//
//  Async.hpp - Async dispatch of functions via executors and locked objects
//
//  Copyright (c) 2015 Brian Fransioli
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt
//

#ifndef AS_ASYNC_HPP
#define AS_ASYNC_HPP

#include "TaskFuture.hpp"
#include "ThreadExecutor.hpp"
#include "TaskImpl.hpp"

#include <atomic>
#include <mutex>
#include <utility>

#include <cassert>

namespace as {

template<class Ex, class Func>
void post(Ex& ex, Func&& func)
{
	ex.schedule( PostTask<Ex,Func>( &ex, std::forward<Func>(func) ) );
}

template<class Ex, class Func, class... Args>
void post(Ex& ex, Func&& func, Args&&... args)
{
	using sc = SplitBy< IsCallable, Func, Args... >;
	using ib = invoker_builder< typename sc::true_types, typename sc::false_types >;

	auto c = ib::build( std::forward<Func>(func), std::forward<Args>(args)... );

	post( ex, [c]() mutable {
			c.invoke();
		} );
}

/// Dispatch a callback in a thread context, i.e. an ExecutionContext
template<class Ex, class Func, class... Args>
TaskFuture< void >
async(Ex& ex, Func&& func, Args&&... args)
{
	using sc = SplitBy< IsCallable, Func, Args... >;
	using ib = invoker_builder< typename sc::true_types, typename sc::false_types >;

	auto c = ib::build( std::forward<Func>(func), std::forward<Args>(args)... );

	post( ex, [c]() mutable {
			c.invoke();
		} );

	return {};
}

/// Dispatch a callback in a new thread context
template<class Func, class... Args>
TaskFuture< decltype( std::declval<Func>()(std::declval<Args>()...) ) >
async(Func&& func, Args&&... args)
{
	ThreadExecutor c;

	return async( c,
	              std::forward<Func>(func),
	              std::forward<Args>(args)... );
}

} // namespace as

#endif // AS_ASYNC_HPP
