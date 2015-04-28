//
//  TaskImpl.hpp - Basic Task Context/Implementation
//
//  Copyright (c) 2015 Brian Fransioli
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt
//

#ifndef AS_TASK_IMPL_HPP
#define AS_TASK_IMPL_HPP

#include "TaskControlBlock.hpp"
#include "CallableTraits.hpp"

#include <memory>
#include <functional>
#include <tuple>

#include <boost/pool/pool_alloc.hpp>

namespace as {

class TaskImpl
{
public:
	virtual ~TaskImpl() {}

	virtual TaskStatus Invoke() = 0;
	virtual void Yield() = 0;
	virtual void Cancel() = 0;
	//virtual bool IsFinished() const = 0;
};

// template<class Invoker, class Result>

template< class Handler >
class TaskImplBase
	: public TaskImpl
{
protected:
	Handler handler;

public:
	TaskImplBase() = default;

	template<class Func, class... Args,
	         class =
	         typename std::enable_if< !std::is_same<TaskImplBase,
	                                                typename std::decay<Func>::type
	                                               >::value
	                                >::type
	        >
	TaskImplBase( Func&& func, Args&&... args )
		: handler( std::bind( std::forward<Func>(func),
		                      std::forward<Args>(args)... ) )
	{}

	TaskImplBase(TaskImplBase&&) = default;

	virtual ~TaskImplBase()
	{}

	virtual TaskStatus Invoke()
	{
		return handler();
	}

	virtual void Yield()
	{}

	virtual void Cancel()
	{
	}
};

// template<class Exec = void, class... >
// struct PostInvoker

template <typename Ret, typename F, typename Tuple, bool Done, int Total, int... N>
struct invoke_impl
{
	static Ret call(F f, Tuple && t)
	{
		return invoke_impl<Ret, F, Tuple, Total == 1 + sizeof...(N), Total, N..., sizeof...(N)>::call(f, std::forward<Tuple>(t));
	}
};

template <typename Ret, typename F, typename Tuple, int Total, int... N>
struct invoke_impl<Ret, F, Tuple, true, Total, N...>
{
	static Ret call(F f, Tuple && t)
	{
		return f(std::get<N>(std::forward<Tuple>(t))...);
	}
};

template<class Func>
struct invocation
{
	typedef Func func_type;
	typedef FunctionSignature<Func> signature_type;
	typedef typename FunctionSignature<Func>::return_type result_type;

	Func func;

	invocation(Func func)
		: func( std::move(func) )
	{}

	template<class... A>
	result_type invoke(A&&... args)
	{
		//constexpr int TSize = std::tuple_size< arg_tuple_type >::value;
		constexpr int TSize = sizeof...( args );
		typedef std::tuple<A...> arg_tuple_type;

		return invoke_impl<result_type, Func, arg_tuple_type, 0 == TSize, TSize>::call(func, std::make_tuple( std::forward<A>(args)... ) );
	}
};

template<class Func, class... Args>
struct full_invocation
	: public invocation<Func>
{
	typedef std::tuple<Args...> arg_tuple_type;

	typedef decltype( std::declval<Func>()( std::declval<Args>()... ) ) result_type;

	arg_tuple_type arg_tuple;

	template<class... A>
	full_invocation(Func func, A&&... args)
		: invocation<Func>( std::move(func) )
		, arg_tuple( std::make_tuple<Args...>( std::forward<A>(args)... ) )
	{}

	result_type invoke()
	{
		constexpr int TSize = std::tuple_size< arg_tuple_type >::value;

		return invoke_impl<result_type, Func, arg_tuple_type, 0 == TSize, TSize>::call(this->func, std::move(arg_tuple) );
	}
};

template<class... Invokers>
struct chain_invocation;

template<class FirstInvocation,
         class SecondInvocation>
struct chain_invocation< FirstInvocation, SecondInvocation >
{
	typedef typename SecondInvocation::result_type result_type;

	FirstInvocation inv1;
	SecondInvocation inv2;

	chain_invocation(FirstInvocation i1,
	                 SecondInvocation i2)
		: inv1( i1 )
		, inv2( i2 )
	{}

	result_type invoke()
	{
		return do_invoke( typename HasArg< typename SecondInvocation::func_type >::type{} );
	}

	template<class... Args>
	result_type invoke( Args&&... args )
	{
		return do_invoke( typename HasArg< typename SecondInvocation::func_type >::type{},
		                  std::forward<Args>(args)... );
	}

private:
	template<class... Args>
	result_type do_invoke( std::true_type, Args&&... args )
	{
		return inv2.invoke( inv1.invoke( std::forward<Args>(args)... ) );
	}

	template<class... Args>
	result_type do_invoke( std::false_type, Args&&... args )
	{
		inv1.invoke( std::forward<Args>(args)... );
		return inv2.invoke();
	}
};

template<class First, class Second, class Third, class... Invokers>
struct chain_invocation<First, Second, Third, Invokers...>
	: chain_invocation<Second, Third, Invokers...>
{
	typedef chain_invocation<Second, Third, Invokers...> base_type;

	typedef typename base_type::result_type result_type;

	First inv1;

	chain_invocation(First i1, Second i2, Third i3, Invokers... invks)
		: base_type( i2, i3, invks... )
		, inv1( i1 )
	{}

	template<class... Args>
	result_type invoke(Args&&... args)
	{
		return do_invoke( typename HasArg< typename Second::func_type >::type{},
		                  std::forward<Args>(args)... );
	}

private:
	template<class... Args>
	result_type do_invoke( std::true_type, Args&&... args )
	{
		return base_type::invoke( inv1.invoke( std::forward<Args>(args)... ) );
	}

	template<class... Args>
	result_type do_invoke( std::false_type, Args&&... args )
	{
		inv1.invoke( std::forward<Args>(args)... );
		return base_type::invoke();
	}
};

template<class First>
struct chain_invocation<First>
	: public First
{};

template <std::size_t... Is>
struct indices {};

template <std::size_t N, std::size_t... Is>
struct build_indices
	: build_indices<N-1, N-1, Is...> {};

template <std::size_t... Is>
struct build_indices<0, Is...> : indices<Is...>
{
	using type = indices<Is...>;
};

template<std::size_t Offset, std::size_t N, std::size_t... Is>
struct build_indices_offset
	: build_indices_offset< Offset, N-1, N-1, Is... >
{};

template<std::size_t Offset, std::size_t... Is>
struct build_indices_offset<Offset, Offset, Is... >
{
	using type = indices< Is... >;
};

template<class Callables, class Args>
struct invoker_builder;

template<class FirstCallable, class... Callables, class... Args>
struct invoker_builder< std::tuple<FirstCallable, Callables...>, std::tuple<Args...> >
{
	typedef full_invocation<FirstCallable, Args...> inv1_type;
	typedef typename inv1_type::result_type inv1_result_type;
	typedef chain_invocation< inv1_type, invocation<Callables>... >  chain_type;
	typedef chain_type result_type;

	template<class C1, class... A, std::size_t... Ids>
	chain_type build_chain_impl(indices<Ids...>, std::tuple<C1> cs, A&&... args )
	{
		return chain_type( inv1_type( std::get<0>(cs), std::forward<A>(args)... ) );
	}

	template<class C1, class... C, class... A, std::size_t... Ids>
	chain_type build_chain_impl(indices<Ids...>, std::tuple<C1, C...> cs, A&&... args )
	{
		return chain_type( inv1_type( std::get<0>(cs), std::forward<A>(args)... ),
		                   std::get<Ids>(cs)... );
	}

	template<class C1, class... C, class... A>
	chain_type build_chain(std::tuple<C1, C...> cs, A&&... args)
	{
		return build_chain_impl( typename build_indices_offset<1, std::tuple_size<decltype(cs)>::value >::type(),
		                         cs,
		                         std::forward<A>(args)... );
	}

	template<class C1, class... C, class... A>
	chain_type operator()( std::tuple<C1, C...> cs, A&&... args )
	{
		return build_chain( cs, std::forward<A>(args)... );
	}
};

template<class Exec, class Func>
struct PostTask
{
	typedef Exec executor_type;
	typedef Func function_type;

	invocation<Func> func;
	Exec *executor;

	PostTask(Exec *ex, Func func)
		: func( std::move(func) )
		, executor(ex)
	{}

	TaskStatus Invoke()
	{
		func.invoke();

		return TaskStatus::Finished;
	}

	void Yield()
	{}

	void Cancel()
	{}
};

template<class Exec, class Func>
struct AsyncTask
{
	typedef Exec executor_type;
	typedef Func function_type;

	invocation<Func> func;
	Exec *executor;

	AsyncTask(Exec *ex, Func func)
		: func( std::move(func) )
		, executor(ex)
	{}

};

} // namespace as

#endif // AS_TASK_IMPL_HPP
