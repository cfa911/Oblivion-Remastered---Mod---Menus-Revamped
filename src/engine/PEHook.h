#pragma once

// ============================================================================================================
// ProcessEvent observers and ownership-scoped interceptors by class, through a vtable ENTRY swap (slot 0x4D) - no code is patched, so this coexists
// with UE4SS's ProcessEvent hook on the shared body in either load order (logic library 7555, 2026-09-29).
//
// Several classes may share one native vtable; each vtable is swapped once, the original pointer is remembered per
// vtable, and the one detour calls every handler registered for the object's class before the original. Handlers
// run on the game thread, before the function body.
// ============================================================================================================

namespace pe
{
	using Handler = void (*)(UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params);
	using Interceptor = bool (*)(UE::UObject*, UE::UFunction*, void*);
	// Returning true skips this call's body. Restrict to owned custom instances,
	// or the exact native hover callback validated by the performance throttle.
	bool Intercept(UE::UClass* a_class, Interceptor a_interceptor);

	// Watches calls on objects whose class is exactly a_class. False (logged) when its vtable cannot be swapped.
	bool Watch(UE::UClass* a_class, Handler a_handler);

	// The same, but the handler runs AFTER the function body (the object may be gone by then: do not rely on it beyond the call).
	bool WatchPost(UE::UClass* a_class, Handler a_handler);

	// Marks one of our own reflected calls (ui::Call, CallFirst, GuardedProcessEvent, reflect::Call): the next ProcessEvent
	// of exactly this object and function skips the watchers, which are for the game's events - our IsVisible walks
	// re-entered the list filter ~38,000 times a second in the container menu (2026-10-29). Interceptors still run, and
	// the calls the engine makes from inside ours are watched as usual.
	class OwnCall
	{
	public:
		OwnCall(UE::UObject* a_obj, UE::UFunction* a_fn);
		~OwnCall();
		OwnCall(const OwnCall&) = delete;
		OwnCall& operator=(const OwnCall&) = delete;

	private:
		UE::UObject*   m_previousObj;   // the mark this one replaced (an own call made from inside a watcher)
		UE::UFunction* m_previousFn;
	};

	// The UFunction's name, UTF-8 (cheap enough for a learn-once compare per UFunction pointer).
	std::string FunctionName(UE::UFunction* a_fn);

	std::string Utf8(const UE::FString& a_s);
}
