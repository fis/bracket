#ifndef BASE_COMMON_H_
#define BASE_COMMON_H_

#include <utility>

#define DISALLOW_COPY(TypeName) \
  TypeName(const TypeName&) = delete; \
  TypeName& operator=(const TypeName&) = delete

#endif // BASE_COMMON_H_

// Local Variables:
// mode: c++
// End:
