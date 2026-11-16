#ifndef PMAC_TERMINAL_H
#define PMAC_TERMINAL_H
#include <stdint.h>
#include <stddef.h>
#ifdef _WIN32
# ifdef PMACT_DYNAMIC
#  define PMACT_API
# else
#  define PMACT_API __declspec(dllimport)
# endif
# define PMACT_CALL __cdecl
#else
# define PMACT_API
# define PMACT_CALL
#endif
#ifdef __cplusplus
extern "C" {
#endif
/* Opaque, process-local handles. One independent SSH client and symbol index per handle. */
typedef uint64_t pmact_handle;
enum { PMACT_OK=0, PMACT_PENDING=1, PMACT_INVALID_ARGUMENT=-1,
       PMACT_INVALID_HANDLE=-2, PMACT_BUFFER_TOO_SMALL=-3, PMACT_BUSY=-4,
       PMACT_INTERNAL=-5, PMACT_NOT_CONNECTED=-6 };
/* All strings and JSON are UTF-8 with explicit byte lengths; no terminating NUL.
 * required reports byte count. Query with out=NULL/capacity=0, allocate, then copy.
 * BUFFER_TOO_SMALL does NOT consume an event. Zero-length strings may use NULL.
 * Keep pointers valid for the duration of a call. Do not alias input/output buffers.
 * Each host must serialize poll_event's query/copy pair for a given handle.
 * Caller must provide valid pointers/capacities; panic containment cannot repair
 * invalid native pointers or use-after-free in the host.
 */
PMACT_API int32_t PMACT_CALL pmact_create(const uint8_t* pmac_dll_path,size_t path_len,pmact_handle* out);
/* JSON fields: host (required), port=22, username="root", password,
 * known_hosts, pinned_sha256, private_key, accept_any_host_key=true, echo=null.
 * echo=null leaves device echo unchanged. Connect automatically loads symbols.
 * Async status is delivered as JSON events; OK means submission accepted.
 */
PMACT_API int32_t PMACT_CALL pmact_connect(pmact_handle,const uint8_t* json,size_t len);
PMACT_API int32_t PMACT_CALL pmact_disconnect(pmact_handle);
PMACT_API int32_t PMACT_CALL pmact_send(pmact_handle,const uint8_t* command,size_t len,uint32_t timeout_ms);
PMACT_API int32_t PMACT_CALL pmact_refresh_symbols(pmact_handle);
/* Nonblocking JSON event: connected, disconnected, progress, symbols_loaded,
 * response {text,elapsed_ms}, error {text}, ready {connected}.
 * ready is emitted after each completed operation; only one operation in flight.
 */
PMACT_API int32_t PMACT_CALL pmact_poll_event(pmact_handle,uint8_t* out,size_t capacity,size_t* required);
/* prefix is the input text up to the cursor. Returns JSON [{text,help},...].
 * Query is local and never sends a device command; special symbols are data.
 */
PMACT_API int32_t PMACT_CALL pmact_suggest(pmact_handle,const uint8_t* prefix,size_t len,uint8_t* out,size_t capacity,size_t* required);
/* Host-owned connection: import JSON array of seven UTF-8 symbol-file strings; [] clears.
 * Does not connect, send commands, or fetch files. Use an idle handle. */
PMACT_API int32_t PMACT_CALL pmact_set_symbol_files(pmact_handle,const uint8_t* json,size_t len);
PMACT_API int32_t PMACT_CALL pmact_last_error(pmact_handle,uint8_t* out,size_t capacity,size_t* required);
/* Requests cancellation, joins the worker, releases runtime resources.
 * May block for native cleanup. Call before unloading the core DLL. */
PMACT_API int32_t PMACT_CALL pmact_destroy(pmact_handle);
#ifdef __cplusplus
}
#endif
#endif