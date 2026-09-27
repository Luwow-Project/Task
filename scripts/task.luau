-- This is the type system for the Task API as it exists in C++ code.

local task = {}

--[=[
	Resumes a function or thread through the scheduler immediately.

	@param functionOrThread The function or thread to resume.
	@param ... Arguments passed to it.
	@return The thread that was resumed.
]=]
task.spawn = (nil :: any) :: <A...>(functionOrThread: ((A...) -> ...any) | thread, A...) -> thread

--[=[
	Resumes a function or thread through the scheduler in the next resumption cycle.

	@param functionOrThread The function or thread to resume.
	@param ... Arguments passed to it.
	@return The scheduled thread.
]=]
task.defer = (nil :: any) :: <A...>(functionOrThread: ((A...) -> ...any) | thread, A...) -> thread

--[=[
	Resumes a function or thread after `t` seconds have elapsed.

	@param t Delay in seconds, negative values are treated as 0.
	@param functionOrThread The function or thread to resume.
	@param ... Arguments passed to it.
	@return The scheduled thread.
]=]
task.delay = (nil :: any) :: <A...>(t: number, functionOrThread: ((A...) -> ...any) | thread, A...) -> thread

--[=[
	Yields the current thread for `t` seconds, and returns the actual elapsed time.

	@param t Time to wait in seconds.
	@return The actual time elapsed in seconds.
]=]
task.wait = (nil :: any) :: (t: number?) -> number

--[=[
	Cancels a pending thread, preventing it from being resumed manually or by the scheduler.

	@param thread The thread to cancel.
]=]
task.cancel = (nil :: any) :: (thread: thread) -> ()

return table.freeze(task)
