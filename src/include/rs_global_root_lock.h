//
// This supplies an interface to a global rw lock that rootspy can use 
// to coordinate access to ROOT objects that use root's global memory. 
// This is necessary since the main program that RootSpy is "spying" on
// must somehow coordinate with RootSpy to ensure histograms etc. are
// not being created or destroyed while RootSpy is copying them.
//
// Root used to supply something that looked like it would do this, but
// it never worked as expected for this use case. Thus, a global
// pthread_rw_lock_t was used and the address of it set to point to the
// one that the main program used. With the introduction of JANA2, however,
// access to the actual object was restricted and only some methods in
// the JGlobalRootLock service can be accessed.
//
// In order not to create a dependency between RootSpy and JANA2, this
// file was introduced that uses templates to allow any class that
// supplies those methods to be used. It also supplies a class that
// implements those methods for a given pthread_rw_lock_t object, thus
// allowing code to use the original methodology of getting a direct
// pointer to the main program's pthread_rw_lock_t object.
//
//  auto rlockptr = japp->GetService<JGlobalRootLock>();
// rslock = RSLockAdapter<JGlobalRootLock>(rlockptr);


#include <pthread.h>
// #include <iostream>

// Base class supplies interface RootSpy code uses.
class RSLock {
public:
    virtual ~RSLock() = default;
    virtual void acquire_read_lock() = 0;
    virtual void acquire_write_lock() = 0;
    virtual void release_lock() = 0;
};

// Templated adapter class.
// It forwards calls to an underlying lock object (of any type)
// that implements the methods acquire_read_lock, acquire_write_lock, and release_lock.
// The methods are purposely made to match what is in JANA2 to ensure compatibility.
template<typename LockType>
class RSLockAdapter : public RSLock {
public:
    explicit RSLockAdapter(const std::shared_ptr<LockType>& lock) : lock_(lock) {}

    void acquire_read_lock() override {
        lock_->acquire_read_lock();
    }
    
    void acquire_write_lock() override {
        lock_->acquire_write_lock();
    }
    
    void release_lock() override {
        lock_->release_lock();
    }
    
private:
    std::shared_ptr<LockType> lock_;
};

// Concrete class for pthread locks.
// This class is a simple wrapper around a pointer to a pthread_rwlock_t,
// implementing the required methods.
class RSPthreadLock : RSLock {
public:
    RSPthreadLock(pthread_rwlock_t* lock=nullptr) : lock_(lock) {
        if(lock_==nullptr){
            pthread_rwlockattr_t attr;
            pthread_rwlockattr_init(&attr);
            pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
            lock_ = new pthread_rwlock_t;
            pthread_rwlock_init(lock_, &attr);
            own_lock_ = true;
        }
    }

    ~RSPthreadLock(){
        if( own_lock_ ){
            pthread_rwlock_destroy(lock_);
            delete lock_;
        }
    }

    void acquire_read_lock() {
        pthread_rwlock_rdlock(lock_);
    }
    
    void acquire_write_lock() {
        pthread_rwlock_wrlock(lock_);
    }
    
    void release_lock() {
        pthread_rwlock_unlock(lock_);
    }
    
private:
    pthread_rwlock_t* lock_;
    bool own_lock_ = false;
};

extern RSLock *gROOTSPY_RW_LOCK;
