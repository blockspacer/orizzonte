// Copyright (c) 2017 Vittorio Romeo
// MIT License |  https://opensource.org/licenses/MIT
// http://vittorioromeo.info | vittorio.romeo@outlook.com

#pragma once

#include "../utility/aligned_storage.hpp"
#include "../utility/cache_aligned_tuple.hpp"
#include "./helper.hpp"
#include <atomic>
#include <type_traits>

namespace orizzonte::node
{
    template <typename... Fs>
    class all : Fs...
    {
    public:
        using in_type = std::common_type_t<typename Fs::in_type...>;
        using out_type = utility::cache_aligned_tuple<typename Fs::out_type...>;

    private:
        struct shared_state
        {
            ORIZZONTE_CACHE_ALIGNED in_type _input;
            ORIZZONTE_CACHE_ALIGNED std::atomic<int> _left;

            template <typename Input>
            shared_state(Input&& input) : _input{FWD(input)}
            {
                // `std::atomic` construction is not atomic.
                _left.store(sizeof...(Fs));
            }
        };

        using shared_state_storage = utility::aligned_storage_for<shared_state>;

        ORIZZONTE_CACHE_ALIGNED shared_state_storage _state;
        ORIZZONTE_CACHE_ALIGNED out_type _values;

    public:
        constexpr all(Fs&&... fs) : Fs{std::move(fs)}...
        {
        }

        template <typename Scheduler, typename Input, typename Then,
            typename Cleanup>
        void execute(Scheduler& scheduler, Input&& input, Then&& then,
            Cleanup&& cleanup) &
        {
            // TODO: don't construct/destroy if lvalue?
            _state.construct(FWD(input));

            meta::enumerate_args(
                [&](auto i, auto t) {
                    auto& f = static_cast<meta::unwrap<decltype(t)>&>(*this);
                    auto computation = [this, &scheduler, &f, then,
                                           cleanup /* TODO: fwd capture */] {
                        f.execute(scheduler, _state->_input,
                            [this, then, cleanup](auto&& out) {
                                utility::get<decltype(i){}>(_values) = out;

                                if(_state->_left.fetch_sub(1) == 1)
                                {
                                    _state.destroy();
                                    then(_values); // TODO: move?
                                    cleanup();
                                }
                            },
                            cleanup);
                    };

                    detail::schedule_if_last<Fs...>(
                        i, scheduler, std::move(computation));
                },
                meta::t<Fs>...);
        }

        static constexpr std::size_t count() noexcept
        {
            return (Fs::count() + ...) + 1;
        }
    };
}
