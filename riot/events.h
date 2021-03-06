#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <thread.h>

typedef enum
{
    EPT_Null = 0,
    EPT_Int,
    EPT_String,
    EPT_Bytes, // shares str_val, but might not be a null terminated string.
    EPT_Raw,
} event_param_type_t;

typedef struct event_param_t
{
    event_param_type_t type;
    union {
        struct {
            const char *str;
            bool needs_free;
            uint16_t len; // 0 -> use strlen
        } str_val;

        int int_val;

        struct {
            uint16_t type;
            union {
                void *ptr;
                uint32_t value;
            } content;
        } raw_val;
    } val;
} event_param_t;

#define EVT_MSG_TYPE ((uint16_t)0x05FF)

#define EVT_NONE       ((uint8_t)0x00)
#define EVT_GPIO       ((uint8_t)0x01) // GPIO interrupt was triggered, params: int gpio_pin
#define EVT_COAP_REQ   ((uint8_t)0x02) // Incoming CoAP req, params: int req_id, int method, [str/bytes payload if present]
#define EVT_COAP_REPLY ((uint8_t)0x03) // Reply to CoAP (server) req, params: int coap_code, int coap_format, [str/bytes payload if present]
#define EVT_COAP_RESP  ((uint8_t)0x04) // Response to CoAP (client) req, params: int req_id, int req_state, [int resp_code, int resp_format, [str/bytes payload if present]]
#define EVT_TIMER      ((uint8_t)0x05) // Previously set timer expired, params: int userdata
#define EVT_UPD_RDY    ((uint8_t)0xFD) // Informs about a code update being ready to reboot into.
#define EVT_GENERIC    ((uint8_t)0xFE) // A generic non-event_t event. Original msg_t contents in first parameters raw_val.
#define EVT_EXIT       ((uint8_t)0xFF) // Exit eventloop asap.

typedef struct event_t
{
    uint8_t id;
    kernel_pid_t sender_pid;
    uint8_t num_params;
    event_param_t *params; // assumed to be in the same buffer as the event itself, won't be freed separately!
} event_t;

// init_events has to be called from the thread that should receive all events
void init_events(void);

// Returns the id of the thread that called init_events, usually the uj one.
kernel_pid_t get_event_pid(void);

// Core functions
void free_event(event_t **eventP);
event_t *wait_event(int timeout_us); // NULL == error, returned event must be freed with free_event.
int post_event(event_t *event); // takes ownership of event, event must be freeable, 0 == success
int post_event_receive(event_t *event, event_t **ret); // takes ownership of event, event must be freeable, ret must be freed with free_event, 0 == success
int reply_last_event(event_t *event); // takes ownership of event, event must be freeable, 0 == success

// All the make_event function return the event passed to them for convenience.

event_t *make_event_raw(uint8_t id, uint8_t num_params);

// For the very simple case
event_t *make_event(uint8_t id);

// Helper to make an event with just int parameters
event_t *make_event_i(uint8_t id, int val);
event_t *make_event_ii(uint8_t id, int val1, int val2);

// Helpers to make an event with one string parameter
// Const strings (_cs* variants) won't be freed
event_t *make_event_cs(uint8_t id, const char *str);
event_t *make_event_s(uint8_t id, const char *str);

// Helpers to make an event with one string and one int parameter
// Const strings (_cs* variants) won't be freed
event_t *make_event_csi(uint8_t id, const char *str, int val);
event_t *make_event_si(uint8_t id, const char *str, int val);
