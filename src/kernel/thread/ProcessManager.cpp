//
// Chino Thread
//
#include "ProcessManager.hpp"
#include "../kdebug.hpp"

extern "C"
{
	uintptr_t g_CurrentThreadContext = 0;
}

using namespace Chino::Thread;

static void OnThreadExit();
static void IdleThreadMain(uintptr_t);

template<typename T>
typename Chino::list<T>::iterator HandleToListIt(HANDLE handle)
{
	using iterator = typename Chino::list<T>::iterator;
	return iterator(reinterpret_cast<typename Chino::list<T>::node*>(handle));
}

template<typename T>
HANDLE ToHandle(typename Chino::list<T>::iterator value)
{
	return reinterpret_cast<HANDLE>(value.node_);
}

ProcessManager::ProcessManager()
	:runningThread_(0), idleProcess_(0)
{

}

HANDLE ProcessManager::CreateProcess(std::string_view name, uint32_t mainThreadPriority, ThreadMain_t entryPoint)
{
	auto it = _processes.emplace_back(name);
	it->AddThread(entryPoint, mainThreadPriority, 0);
	return ToHandle<Process>(it);
}

void ProcessManager::AddReadyThread(HANDLE handle)
{
	auto it = HandleToListIt<Thread>(handle);
	auto priority = it->GetPriority();
	kassert(priority < readyThreads_.size());
	readyThreads_[priority].emplace_back(handle);
	kassert(!readyThreads_[priority].empty());
}

void ProcessManager::StartScheduler()
{
	kassert(!idleProcess_);
	idleProcess_ = CreateProcess("System Idle", 0, IdleThreadMain);

	ArchSetupSchedulerTimer();
	ArchHaltProcessor();
}

ProcessManager::thread_handle_it ProcessManager::SelectNextSwitchToThread()
{
	// Round robin in threads of same priority
	thread_handle_it threadSwitchTo;
	if (runningThread_.good())
	{
		auto next = runningThread_;
		if ((++next).good())
			threadSwitchTo = next;
	}

	if (!threadSwitchTo.good())
	{
		for (auto it = readyThreads_.rbegin(); it != readyThreads_.rend(); ++it)
		{
			if (!it->empty())
			{
				threadSwitchTo = it->begin();
				break;
			}
		}
	}

	kassert(threadSwitchTo.good());
	return threadSwitchTo;
}

ThreadContext_Arch& ProcessManager::SwitchThreadContext()
{
	auto nextThread = SelectNextSwitchToThread();
	runningThread_ = nextThread;
	auto& arch = HandleToListIt<Thread>(*nextThread)->GetContext();
#if 0
	g_Logger->PutFormat("RSP: %lx\n", arch.rsp);
#endif
	return arch;
}

ProcessManager::Process & ProcessManager::GetProcess(HANDLE handle)
{
	kassert(handle);
	return *HandleToListIt<Process>(handle);
}

ProcessManager::Process::Process(std::string_view name)
	:name_(name)
{
}

HANDLE ProcessManager::Process::AddThread(ThreadMain_t entryPoint, uint32_t priority, uintptr_t parameter)
{
	auto handle = ToHandle<Thread>(threads_.emplace_back(entryPoint, priority, parameter));
	g_ProcessMgr->AddReadyThread(handle);
	return handle;
}

ProcessManager::Thread::Thread(ThreadMain_t entryPoint, uint32_t priority, uintptr_t parameter)
	:priority_(priority), threadContext_({})
{
	kassert(priority <= MAX_THREAD_PRIORITY);
	auto stackSize = DEFAULT_THREAD_STACK_SIZE;
	stack_ = std::make_unique<uint8_t[]>(stackSize);
	auto stackPointer = uintptr_t(stack_.get()) + stackSize;
	ArchInitializeThreadContextArch(&threadContext_, stackPointer, uintptr_t(entryPoint), uintptr_t(OnThreadExit), parameter);
}

static void OnThreadExit()
{
	kassert(!"Exit unexpected.");
}

static void IdleThreadMain(uintptr_t)
{
	while (1)
	{
		for (size_t i = 0; i < 100; i++)
			ArchHaltProcessor();
		g_Logger->PutChar(L'.');
	}
}

extern "C" void Kernel_SwitchThreadContext()
{
	g_CurrentThreadContext = uintptr_t(&g_ProcessMgr->SwitchThreadContext());
}
