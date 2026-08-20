// runtime/sync/src/channel.cpp — Channel template explicit instantiations.
//
// The channel is a thread-safe MPSC queue using a mutex + condvar. For
// the prototype we instantiate the common types.
#include "aegis/runtime/sync/channel.hpp"
namespace aegis::runtime::sync {
template class Channel<int32_t>;
template class Channel<int64_t>;
template class Channel<uint32_t>;
template class Channel<uint64_t>;
} // namespace aegis::runtime::sync
