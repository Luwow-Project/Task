# Task
A Roblox-compatible task library for Luwow-Project.

This repository is not stand-alone. You must build it externally from within another
repository like https://github.com/Luwow-Project/Release

## Project Overview

This project contains the sources for Luwow Task library and the executable `runscriptwithtasklib`.

This library requires the external library [libuv](https://github.com/libuv/libuv) as a dependency, add it to your CMakeLists.txt file using `LIBUV_ROOT`.

## Library Documentation

### `task.spawn(functionOrThread: (...any) -> (any) | thread, ...) -> thread`

Resumes a function/thread through the scheduler immediately.

### `task.defer(functionOrThread: (...any) -> (any) | thread, ...) -> thread`

Resumes a function/thread through the scheduler in the next resumption cycle.

### `task.delay(t: number,  functionOrThread: (...any) -> (any) | thread, ...) -> thread`

Resumes a function/thread through the scheduler after `t` seconds.

### `task.wait(t: number?) -> number`

Yields the thread for `t` seconds, and returns the actual elapsed time.

### `task.cancel(th: thread)`

Cancels a pending thread, preventing it from being resumed manually or by the scheduler.