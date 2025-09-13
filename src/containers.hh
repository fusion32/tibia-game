#ifndef TIBIA_CONTAINERS_HH_
#define TIBIA_CONTAINERS_HH_ 1

#include "common.hh"

// NOTE(fusion): All containers are automatically managed by their constructors
// and destructors, meaning raw copies could result in memory corruption through
// double frees, use after frees, etc... To avoid that, we need to make them all
// non copyable.
#define NONCOPYABLE(Type)						\
	Type(const Type &Other) = delete;			\
	void operator=(const Type &Other) = delete;

// NOTE(fusion): What the actual fuck. This is an ever growing dynamic array
// with each access through `operator()` growing it to make sure the requested
// index is valid, even for negative indices.
template<typename T>
struct vector{
	NONCOPYABLE(vector)

	vector(int min, int max, int block){
		int space = (max - min) + 1;
		if(space < 1){
			error(Translate("vector: Ungueltige Feldgroesse %d bis %d.\n",
							"vector: Invalid field size %d to %d.\n"), min, max);
			space = 1;
		}

		if(block < 0){
			error(Translate("vector: Ungueltige Blockgroesse %d.\n",
							"vector: Invalid block size %d.\n"), block);
			block = 0;
		}

		this->min = min;
		this->max = max;
		this->start = min;
		this->space = space;
		this->block = block;
		this->initialized = false;
		this->entry = new T[this->space];
	}

	vector(int min, int max, int block, T init) : vector(min, max, block) {
		this->initialized = true;
		this->init = init;
		for(int i = 0; i < this->space; i += 1){
			this->entry[i] = init;
		}
	}

	~vector(void){
		delete[] this->entry;
	}

	T *at(int index){
		// TODO(fusion): This is probably not the best way to achieve this.

		while(index < this->start){
			int increment = this->block;
			if(increment == 0){
				increment = this->space;
			}

			T *entry = new T[this->space + increment];
			for(int i = this->min; i <= this->max; i += 1){
				int old_index = i - this->start;
				int new_index = old_index + increment;

				// TODO(fusion): Do we actually need to swap elements here? I'm
				// assuming some non-trivial structures that would invoke their
				// destructors when `this->entry` gets deleted just below.
				std::swap(entry[new_index], this->entry[old_index]);
			}

			if(this->entry != NULL){
				delete[] this->entry;
			}
			this->entry = entry;
			this->start -= increment;
			this->space += increment;
		}

		while(index >= (this->start + this->space)){
			int increment = this->block;
			if(increment == 0){
				increment = this->space;
			}

			T *entry = new T[this->space + increment];
			for(int i = this->min; i <= this->max; i += 1){
				int old_index = i - this->start;
				int new_index = old_index;

				// TODO(fusion): Same as above.
				std::swap(entry[new_index], this->entry[old_index]);
			}

			if(this->entry != NULL){
				delete[] this->entry;
			}
			this->entry = entry;
			this->space += increment;
		}

		while(index < this->min){
			this->min -= 1;
			if(this->initialized){
				this->entry[this->min - this->start] = this->init;
			}
		}

		while(index > this->max){
			this->max += 1;
			if(this->initialized){
				this->entry[this->max - this->start] = this->init;
			}
		}

		return &this->entry[index - this->start];
	}

	T copyAt(int index) const {
		T Result = {};
		if(index >= this->start && index < (this->start + this->space)){
			Result = this->entry[index - this->start];
		}else if(this->initialized){
			Result = this->init;
		}
		return Result;
	}

	// DATA
	// =================
	int min;
	int max;
	int start;
	int space;
	int block;
	bool initialized;
	T init;
	T *entry;
};

template<typename K, typename T>
struct priority_queue_entry{
	K Key;
	T Data;
};

template<typename K, typename T>
struct priority_queue{
	NONCOPYABLE(priority_queue)

	priority_queue(int capacity, int increment){
		Entry = new vector<priority_queue_entry<K, T>>(1, capacity, increment);
		Entries = 0;
	}

	~priority_queue(void){
		delete Entry;
	}

	void insert(K Key, T Data){
		this->Entries += 1;
		int CurrentIndex = this->Entries;
		*this->Entry->at(CurrentIndex) = {Key, Data};
		while(CurrentIndex > 1){
			int ParentIndex = CurrentIndex / 2;
			priority_queue_entry<K, T> *Current = this->Entry->at(CurrentIndex);
			priority_queue_entry<K, T> *Parent = this->Entry->at(ParentIndex);
			if(Parent->Key <= Current->Key)
				break;
			std::swap(*Current, *Parent);
			CurrentIndex = ParentIndex;
		}
	}

	void deleteMin(void){
		if(this->Entries < 1){
			error(Translate("priority_queue::deleteMin: Warteschlange ist leer.\n",
							"priority_queue::deleteMin: Queue is empty.\n"));
			return;
		}

		if(this->Entries > 1){
			int CurrentIndex = 1;
			int LastIndex = this->Entries;
			std::swap(*this->Entry->at(CurrentIndex),
					*this->Entry->at(LastIndex));

			// TODO(fusion): This may be an oversight but the decompiled version
			// checks in the loop below would INCLUDE `LastIndex`, which I assume
			// is a bug? The first and last elements were just swapped and what
			// was the first element in the queue is now at the end and should be
			// considered "removed".
			while(1){
				int SmallestIndex = CurrentIndex * 2;
				if(SmallestIndex >= LastIndex){
					break;
				}

				priority_queue_entry<K, T> *Smallest = this->Entry->at(SmallestIndex);
				if((SmallestIndex + 1) < LastIndex){
					priority_queue_entry<K, T> *Other = this->Entry->at(SmallestIndex + 1);
					if(Other->Key < Smallest->Key){
						Smallest = Other;
						SmallestIndex += 1;
					}
				}

				priority_queue_entry<K, T> *Current = this->Entry->at(CurrentIndex);
				if(Current->Key <= Smallest->Key){
					break;
				}

				std::swap(*Current, *Smallest);
				CurrentIndex = SmallestIndex;
			}
		}

		this->Entries -= 1;
	}

	// DATA
	// =================
	vector<priority_queue_entry<K, T>> *Entry;
	int Entries;
};

template<typename T>
struct matrix{
	NONCOPYABLE(matrix)

	matrix(int xmin, int xmax, int ymin, int ymax){
		int dx = (xmax - xmin) + 1;
		int dy = (ymax - ymin) + 1;

		if(dx < 1 || dy < 1){
			error(Translate("matrix: Ungueltige Feldgroesse %d..%d, %d..%d.\n",
							"matrix: Invalid field size %d..%d, %d..%d.\n"), xmin, xmax, ymin, ymax);

			if(dx < 1){
				dx = 1;
			}

			if(dy < 1){
				dy = 1;
			}
		}

		this->xmin = xmin;
		this->ymin = ymin;
		this->dx = dx;
		this->dy = dy;
		this->entry = new T[dx * dy];
	}

	matrix(int xmin, int xmax, int ymin, int ymax, T init) : matrix(xmin, xmax, ymin, ymax) {
		int count = this->dx * this->dy;
		for(int i = 0; i < count; i += 1){
			this->entry[i] = init;
		}
	}

	~matrix(void){
		delete[] this->entry;
	}

	// NOTE(fusion): Same as `at` but returns NULL on out of bounds coordinates.
	T *boundedAt(int x, int y){
		int xoffset = x - this->xmin;
		int yoffset = y - this->ymin;
		if(xoffset < 0 || xoffset >= this->dx || yoffset < 0 || yoffset >= this->dy){
			return NULL;
		}else{
			return &this->entry[yoffset * this->dx + xoffset];
		}
	}

	T *at(int x, int y){
		int xoffset = x - this->xmin;
		int yoffset = y - this->ymin;
		if(xoffset < 0 || xoffset >= this->dx || yoffset < 0 || yoffset >= this->dy){
			error(Translate("matrix::operator(): Ungueltiger Index %d/%d.\n",
							"matrix::operator(): Invalid index %d/%d.\n"), x, y);
			return &this->entry[0];
		}else{
			// TODO(fusion): Are we really storing this in row major order?
			return &this->entry[yoffset * this->dx + xoffset];
		}
	}

	// DATA
	// =================
	int xmin;
	int ymin;
	int dx;
	int dy;
	T *entry;
};

template<typename T>
struct matrix3d{
	NONCOPYABLE(matrix3d)

	matrix3d(int xmin, int xmax, int ymin, int ymax, int zmin, int zmax){
		int dx = (xmax - xmin) + 1;
		int dy = (ymax - ymin) + 1;
		int dz = (zmax - zmin) + 1;

		if(dx < 1 || dy < 1 || dz < 1){
			error(Translate("matrix3d: Ungueltige Feldgroesse %d..%d, %d..%d, %d..%d.\n",
							"matrix3d: Invalid field size %d..%d, %d..%d, %d..%d.\n"),
					xmin, xmax, ymin, ymax, zmin, zmax);

			if(dx < 1){
				dx = 1;
			}

			if(dy < 1){
				dy = 1;
			}

			if(dz < 1){
				dz = 1;
			}
		}

		this->xmin = xmin;
		this->ymin = ymin;
		this->zmin = zmin;
		this->dx = dx;
		this->dy = dy;
		this->dz = dz;
		this->entry = new T[dx * dy * dz];
	}

	matrix3d(int xmin, int xmax, int ymin, int ymax, int zmin, int zmax, T init)
			: matrix3d(xmin, xmax, ymin, ymax, zmin, zmax) {
		int count = this->dx * this->dy * this->dz;
		for(int i = 0; i < count; i += 1){
			this->entry[i] = init;
		}
	}

	~matrix3d(void){
		delete[] this->entry;
	}

	T *at(int x, int y, int z){
		int xoffset = x - this->xmin;
		int yoffset = y - this->ymin;
		int zoffset = z - this->zmin;
		if(xoffset < 0 || xoffset >= this->dx
				|| yoffset < 0 || yoffset >= this->dy
				|| zoffset < 0 || zoffset >= this->dz){
			error(Translate("matrix3d::operator(): Ungueltiger Index %d/%d/%d.\n",
							"matrix3d::operator(): Invalid index %d/%d/%d.\n"), x, y, z);
			return &this->entry[0];
		}else{
			// TODO(fusion): Same as `matrix::at` on the XY plane.
			return &this->entry[zoffset * this->dx * this->dy
								+ yoffset * this->dx
								+ xoffset];
		}
	}

	// DATA
	// =================
	int xmin;
	int ymin;
	int zmin;
	int dx;
	int dy;
	int dz;
	T *entry;
};

template<typename T>
struct listnode{
	listnode *next;
	listnode *prev;
	T data;
};

template<typename T>
struct list{
	NONCOPYABLE(list)

	list(void){
		firstNode = NULL;
		lastNode = NULL;
	}

	~list(void){
		while(this->firstNode != NULL){
			this->remove(this->firstNode);
		}
	}

	listnode<T> *append(void){
		listnode<T> *node = new listnode<T>;
		node->next = NULL;
		node->prev = NULL;
		if(this->firstNode == NULL){
			ASSERT(this->lastNode == NULL);
			this->firstNode = node;
		}else{
			ASSERT(this->lastNode != NULL);
			this->lastNode->next = node;
			node->prev = this->lastNode;
		}
		this->lastNode = node;
		return node;
	}

	void remove(listnode<T> *node){
		if(node == NULL){
			error(Translate("list::remove: node ist NULL.\n",
							"list::remove: node is NULL.\n"));
			return;
		}

		if(node->prev == NULL){
			ASSERT(this->firstNode == node);
			this->firstNode = node->next;
		}else{
			node->prev->next = node->next;
		}

		if(node->next == NULL){
			ASSERT(this->lastNode == node);
			this->lastNode = node->prev;
		}else{
			node->next->prev = node->prev;
		}

		delete node;
	}

	// DATA
	// =================
	listnode<T> *firstNode;
	listnode<T> *lastNode;
};

template<typename T>
struct fifo{
	NONCOPYABLE(fifo)

	fifo(int InitialSize){
		ASSERT(InitialSize > 0);
		this->Entry = new T[InitialSize];
		this->Size = InitialSize;
		this->Head = -1;
		this->Tail = 0;
	}

	~fifo(void){
		delete[] this->Entry;
	}

	T *next(void){
		T *Next = NULL;
		if(this->Tail <= this->Head){
			Next = &this->Entry[this->Tail % this->Size];
		}
		return Next;
	}

	T *append(void){
		if((this->Head - this->Tail + 1) == this->Size){
			int NewSize = this->Size * 2;
			T *NewEntry = new T[NewSize];
			// TODO(fusion): Is it even possible to have `this->Entry == NULL`?
			if(this->Entry != NULL){
				for(int Index = this->Tail; Index <= this->Head; Index += 1){
					NewEntry[Index % NewSize] = this->Entry[Index % this->Size];
				}
				delete[] this->Entry;
			}
			this->Entry = NewEntry;
			this->Size = NewSize;
		}

		// TODO(fusion): We don't consider integer overflow at all.
		this->Head += 1;
		return &this->Entry[this->Head % this->Size];
	}

	void remove(void){
		if(this->Tail > this->Head){
			error(Translate("fifo::remove: Fifo ist leer.\n",
							"fifo::remove: Fifo is empty.\n"));
			return;
		}

		this->Tail += 1;
	}

	int iterFirst(void){
		return this->Head;
	}

	int iterLast(void){
		return this->Tail;
	}

	T *iterNext(int *Position){
		if(*Position < this->Tail || this->Head < *Position){
			return NULL;
		}
		T *Result = &this->Entry[*Position % this->Size];
		*Position -= 1;
		return Result;
	}

	T *iterPrev(int *Position){
		if(*Position < this->Tail || this->Head < *Position){
			return NULL;
		}

		T *Result = &this->Entry[*Position % this->Size];
		*Position += 1;
		return Result;
	}

	// TODO(fusion): There is also a `fifoIterator` used a few times and it is
	// essentially iterating from `this->Head` towards `this->Tail`. All its
	// functions were inlined so I'm not sure it is needed.

	// DATA
	// =================
    T *Entry;
    int Size;
    int Head;
    int Tail;
};

template<typename T>
union storeitem{
	// IMPORTANT(fusion): This will only work properly with POD structures. We
	// could also manually handle `data` construction and destruction but I don't
	// think we need it.
	STATIC_ASSERT(std::is_trivially_default_constructible<T>::value
			&& std::is_trivially_destructible<T>::value
			&& std::is_trivially_copyable<T>::value);
	storeitem<T> *next;
	T data;
};

template<typename T, usize N>
struct storeunit{
	STATIC_ASSERT(N > 0);
	storeitem<T> item[N];
};

// NOTE(fusion): The `store` container is an allocator that manages a single type.
// It is also known as a slab allocator.
template<typename T, usize N>
struct store{
	NONCOPYABLE(store)

	store(void){
		this->Units = new list<storeunit<T, N>>;
		this->firstFreeItem = NULL;
	}

	~store(void){
		delete this->Units;
	}

	T *getFreeItem(void){
		if(this->firstFreeItem == NULL){
			storeunit<T, N> *Unit = &this->Units->append()->data;
			for(usize i = 0; i < (N - 1); i += 1){
				Unit->item[i].next = &Unit->item[i + 1];
			}
			Unit->item[N - 1].next = NULL;
			this->firstFreeItem = &Unit->item[0];
		}

		storeitem<T> *Item = this->firstFreeItem;
		this->firstFreeItem = Item->next;
		return &Item->data;
	}

	void putFreeItem(T *Item){
		// TODO(fusion): Not the safest thing to do.
		ASSERT(Item != NULL);
		((storeitem<T>*)Item)->next = this->firstFreeItem;
		this->firstFreeItem = (storeitem<T>*)Item;
	}

	// DATA
	// =================
	list<storeunit<T, N>> *Units;
	storeitem<T> *firstFreeItem;
};

#endif //TIBIA_CONTAINERS_HH_