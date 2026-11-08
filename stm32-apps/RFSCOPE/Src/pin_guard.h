#ifndef PIN_GUARD_H
#define PIN_GUARD_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { PIN_USER=0, PIN_RESERVED=1, PIN_CORE=2, PIN_UNKNOWN=3 } pin_class_t;
typedef enum { PIN_IDLE=0, PIN_ACTIVE=1, PIN_FAULT=2 } pin_activity_t;
typedef enum { PIN_DENIED=0, PIN_PENDING_RESTART=1, PIN_ALLOWED=2 } pin_result_t;
typedef struct { uint8_t port, number; pin_class_t classification; pin_activity_t activity; bool owned; } pin_info_t;
typedef struct { uint8_t port, number; uint32_t generation; bool valid; } pin_request_t;
/* Port is 0=A, 1=B etc. A physical disconnect cannot be inferred from GPIO state alone. */
pin_result_t pin_guard_request(const pin_info_t *pin, pin_request_t *pending, uint32_t boot_generation);
bool pin_guard_apply(const pin_info_t *pin, const pin_request_t *pending, uint32_t boot_generation, bool physically_verified, bool electrical_verified);
#endif
