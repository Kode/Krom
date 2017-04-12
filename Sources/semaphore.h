#pragma once

class Semaphore {
public:
	Semaphore(int count);
	~Semaphore();
	void wait();
	void signal();
private:
#ifdef _WIN32
	void* semaphore;
#endif
};
