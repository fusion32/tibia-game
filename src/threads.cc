#include "threads.hh"

struct TThreadStarter {
	ThreadFunction *Function;
	void *Argument;
	bool Detach;
};

static void *ThreadStarter(void *Pointer){
	TThreadStarter *Starter = (TThreadStarter*)Pointer;
	ThreadFunction *Function = Starter->Function;
	void *Argument = Starter->Argument;
	bool Detach = Starter->Detach;
	delete Starter;

	int Result = Function(Argument);

	// TODO(fusion): Just store the integer as a pointer and avoid allocation?
	int *ResultPointer = NULL;
	if(!Detach){
		ResultPointer = new int;
		*ResultPointer = Result;
	}
	pthread_exit(ResultPointer);

	return NULL; // Unreachable.
}

ThreadHandle StartThread(ThreadFunction *Function, void *Argument, bool Detach){
	TThreadStarter *Starter = new TThreadStarter;
	Starter->Function = Function;
	Starter->Argument = Argument;
	Starter->Detach = Detach;

	pthread_t Handle;
	int err = pthread_create(&Handle, NULL, ThreadStarter, Starter);
	if(err != 0){
		error(Translate("StartThread: Kann Thread nicht anlegen; Fehlercode %d.\n",
						"StartThread: Cannot create thread; Error code %d.\n"), err);
		return INVALID_THREAD_HANDLE;
	}

	if(Detach){
		pthread_detach(Handle);
	}

	return (ThreadHandle)Handle;
}

ThreadHandle StartThread(ThreadFunction *Function, void *Argument, size_t StackSize, bool Detach){
	TThreadStarter *Starter = new TThreadStarter;
	Starter->Function = Function;
	Starter->Argument = Argument;
	Starter->Detach = Detach;

	pthread_t Handle;
	pthread_attr_t Attr;
	pthread_attr_init(&Attr);
	pthread_attr_setstacksize(&Attr, StackSize);
	int err = pthread_create(&Handle, &Attr, ThreadStarter, Starter);
	pthread_attr_destroy(&Attr);
	if(err != 0){
		error(Translate("StartThread: Kann Thread nicht anlegen; Fehlercode %d.\n",
						"StartThread: Cannot create thread; Error code %d.\n"), err);
		return INVALID_THREAD_HANDLE;
	}

	if(Detach){
		pthread_detach(Handle);
	}

	return (ThreadHandle)Handle;
}

ThreadHandle StartThread(ThreadFunction *Function, void *Argument, void *Stack, size_t StackSize, bool Detach){
	TThreadStarter *Starter = new TThreadStarter;
	Starter->Function = Function;
	Starter->Argument = Argument;
	Starter->Detach = Detach;

	pthread_t Handle;
	pthread_attr_t Attr;
	pthread_attr_init(&Attr);
	pthread_attr_setstack(&Attr, Stack, StackSize);
	int err = pthread_create(&Handle, &Attr, ThreadStarter, Starter);
	pthread_attr_destroy(&Attr);
	if(err != 0){
		error(Translate("StartThread: Kann Thread nicht anlegen; Fehlercode %d.\n",
						"StartThread: Cannot create thread; Error code %d.\n"), err);
		return INVALID_THREAD_HANDLE;
	}

	if(Detach){
		pthread_detach(Handle);
	}

	return (ThreadHandle)Handle;
}

int JoinThread(ThreadHandle Handle){
	int Result = 0;
	int *ResultPointer;

	pthread_join((pthread_t)Handle, (void**)&ResultPointer);
	if(ResultPointer != NULL){
		Result = *ResultPointer;
		delete ResultPointer;
	}

	return Result;
}

void DelayThread(int Seconds, int MicroSeconds){
	if(Seconds == 0 && MicroSeconds == 0){
		sched_yield();
	}else if(MicroSeconds == 0){
		sleep(Seconds);
	}else{
		usleep(MicroSeconds + Seconds * 1000000);
	}
}

Semaphore::Semaphore(int Value){
	this->value = Value;

	// TODO(fusion): These should probably be non-recoverable errors.

	if(pthread_mutex_init(&this->mutex, NULL) != 0){
		error(Translate("Semaphore::Semaphore: Kann Mutex nicht einrichten.\n",
						"Semaphore::Semaphore: Cannot set mutex.\n"));
	}

	if(pthread_cond_init(&this->condition, NULL) != 0){
		error(Translate("Semaphore::Semaphore: Kann Wartebedingung nicht einrichten.\n",
						"Semaphore::Semaphore: Cannot set wait condition.\n"));
	}
}

Semaphore::~Semaphore(void){
	// IMPORTANT(fusion): Due to how initialization is rolled out, `exit` may be
	// called after threads are spawned but before `ExitAll` is registered as an
	// exit handler. This means such threads may still be running or left global
	// semaphores in an inconsistent state if abruptly terminated. Either way,
	// they are still considered "in use".
	//  In this case, calling `destroy` on either mutex or condition variable is
	// undefined behaviour as per the manual but the actual implementation would
	// fail on `mutex_destroy` with `EBUSY` and hang on `cond_destroy`.
	//	The temporary solution is to check the result from `mutex_destroy` before
	// attempting to call `cond_destroy` to avoid hanging at exit.

	int ErrorCode;
	if((ErrorCode = pthread_mutex_destroy(&this->mutex)) != 0){
		error(Translate("Semaphore::~Semaphore: Kann Mutex nicht freigeben: (%d) %s.\n",
						"Semaphore::~Semaphore: Cannot release mutex: (%d) %s.\n"),
				ErrorCode, strerrordesc_np(ErrorCode));
	}else if((ErrorCode = pthread_cond_destroy(&this->condition)) != 0){
		error(Translate("Semaphore::~Semaphore: Kann Wartebedingung nicht freigeben: (%d) %s.\n",
						"Semaphore::~Semaphore: Cannot release wait condition: (%d) %s.\n"),
				ErrorCode, strerrordesc_np(ErrorCode));
	}
}

void Semaphore::down(void){
	pthread_mutex_lock(&this->mutex);
	while(this->value <= 0){ // TODO(fusion): Make sure this is always a load operation?
		pthread_cond_wait(&this->condition, &this->mutex);
	}
	this->value -= 1;
	pthread_mutex_unlock(&this->mutex);
}

void Semaphore::up(void){
	pthread_mutex_lock(&this->mutex);
	this->value += 1;
	pthread_mutex_unlock(&this->mutex);
	pthread_cond_signal(&this->condition);
}
