#pragma once

class Semaphore {
public:
	Semaphore(int count);
	~Semaphore();
	void wait();
	void signal();
private:
#ifdef SYS_WINDOWS
	void* semaphore;
#endif
};
