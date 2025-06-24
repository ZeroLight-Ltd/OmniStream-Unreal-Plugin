// Copyright ZeroLight ltd. All Rights Reserved.
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct AbortHandle AbortHandle;

typedef struct BuildContext BuildContext;

/**
 * Opaque pointer to a bard client object
 */
typedef struct Client Client;

/**
 * Opaque object representing the configuration of a portal client. Pass to [crate::client_new_from_config]
 */
typedef struct Config Config;

typedef struct EnsureTokenResult EnsureTokenResult;

typedef struct EnsureTokenTask EnsureTokenTask;

typedef struct OneStepUploadResult OneStepUploadResult;

typedef struct OneStepUploadTask OneStepUploadTask;

typedef struct OpenSelectAssetLineGuiResult OpenSelectAssetLineGuiResult;

typedef struct OpenSelectAssetLineGuiTask OpenSelectAssetLineGuiTask;

/**
 * Opaque object
 */
typedef struct OutputMessage OutputMessage;

/**
 * Opaque pointer to an error object
 */
typedef struct PortalError PortalError;

typedef struct PortalUploadResult PortalUploadResult;

typedef struct PortalUploadTask PortalUploadTask;

/**
 * Opaque object
 */
typedef struct ProgressStream ProgressStream;

typedef struct UploadResult UploadResult;

typedef struct UploadTask UploadTask;

/**
 * Struct to aid in string FFI. Contains a pointer to normal c-string but must be passed back to
 * [crate::ffi_string_destroy] rather than being freed with the c-allocator
 */
typedef struct FFIString {
  char *c_str;
} FFIString;

/**
 * Pointer to a memory buffer containing UTF8 encoded bytes + length of buffer.
 * The buffer cannot be larger than 4Gb (2^32 bytes).
 * (Fixed sized integer is used to ensure compatability with langauges without a size_t types,
 * such as C# for unity)
 */
typedef struct UTF8InputString {
  const uint8_t *pointer;
  uint32_t length_in_bytes;
} UTF8InputString;

/**
 * There are many params for one step uploads, so stuff them in a struct
 *
 * Note on intermeidate storage providers (ISP). You may set a ISP,
 * disable any use of an ISP or say you don't care use the action default.
 *
 * intermediate_storge_provider | disable_intermeidate_provider | outcome
 * -------------------------------------------------------------------------
 * unset                        | false                         | use server side default
 * set                          | false                         | use an ISP
 * unset                        | true                          | don't use an ISP, even if that's the default on the server
 * set                          | true                          | error/doesn't make sense
 *
 */
typedef struct OneStepUploadParams {
  /**
   * An asset verison id. If this is specified, an attempt will be made to
   * resume uploading to an existing asset version id. If not set, a fresh
   * asset version will be created
   */
  struct UTF8InputString asset_version_id;
  struct UTF8InputString asset_line_id;
  struct UTF8InputString storage_provider;
  struct UTF8InputString intermediate_storage_provider;
  bool disable_intermeidate_provider;
  struct UTF8InputString directory;
  struct UTF8InputString extension_data;
  struct UTF8InputString thumbnail;
  uint64_t chunk_size;
} OneStepUploadParams;

typedef struct OpenSelectAssetLineParams {
  struct UTF8InputString suggested_asset_line;
} OpenSelectAssetLineParams;

/**
 * Result of calling progress_stream_get_next_message
 * output_message may be null if an error occurred or there are no more messages
 * if output_message was null, check the success flag. true => no more message,
 * false => an error occurred
 */
typedef struct GetNextMessageResult {
  struct OutputMessage *output_message;
  bool success;
} GetNextMessageResult;

/**
 * Should get mulched down to a static pointer in the c interface
 */
typedef const char *VersionString;

extern const VersionString PORTAL_CLIENT_LIBRARY_VERSION;

bool start_logging(const char *filename, bool unbuffered, bool truncate_log_file);

/**
 * Start debug logging with default parameters
 */
void start_simple_logging(bool truncate_log_file);

/**
 * Get the version of this library as an allocated string
 */
struct FFIString get_library_version(void);

/**
 * Start debug logging with default parameters - do not buffer IO, this will make logging much slower
 * but may mean more output reaches disk if something bad happened
 */
void start_simple_logging_unbuffered(bool truncate_log_file);

/**
 * Takes the last error to have occured from thread local storage. Note that the error is "taken"
 * and the thread local storage will be cleared so that if this function is called twice
 * it will return a null pointer the second time.
 */
struct PortalError *take_last_error(void);

/**
 * Given an error, render the error as a human readable string
 */
struct FFIString error_get_message(struct PortalError *error);

/**
 * Destory an error
 */
void error_destory(struct PortalError *error);

/**
 * Use a [Config] object to create a [Client]. Important: The [Config] is *consumed* and becomes invalid
 * after this function returns. Do not try to also destroy the [Config], this wil case **undefined behavior**. Do not
 * Reuse the config object by calling this function a second time on the same argument, this will cause **undefined behavior**
 *
 * Optionally, a build context may be passed in, in which case the state of the client and any ongoing build will be
 * restored to that of the build context
 *
 * If a build context is passed in, the client takes ownership of that build context. In this case **there is no need to destroy**
 * the context
 */
struct Client *client_new_from_config(struct Config *opts,
                                      struct BuildContext *cx);

/**
 * Destroys a [Client]
 */
void client_destroy(struct Client *client);

/**
 * Extracts the state of any ongoing build into a build context. This may be used at a later date
 * date to create a new client the same state with regards to the progress of builds as this client
 */
struct BuildContext *client_get_build_context(struct Client *client);

/**
 * Destroys a [BuildContext].
 */
void build_context_destroy(struct BuildContext *cx);

/**
 * If a token has not yet being obtained, opens a browser inviting the user to log in.
 * Blocks until the token is fetched
 */
bool client_ensure_token_is_fetched(struct Client *client);

/**
 * Gets an abort handle of a task. This may be used to cancel that task
 */
struct AbortHandle *ensure_token_task_get_abort_handle(struct EnsureTokenTask *task);

/**
 * Waits on the result of an async task. A task can only be waited on once.
 * After this function is called, the pointer to the task will be invalid. If there might be a need to
 * cancel the task use $get_abort_handle_name() to get an abort handle first
 * Produces a task result
 */
struct EnsureTokenResult *ensure_token_task_wait(struct Client *client,
                                                 struct EnsureTokenTask *task);

/**
 * Check if the task was cancelled. It is an error to try to get the output from a task that was
 * cancelled so this function must be called before trying to get the output
 */
bool ensure_token_task_result_was_cancelled(struct EnsureTokenResult *task_result);

/**
 * obtain the output from a task result.
 */
void *ensure_token_task_result_into_output(struct EnsureTokenResult *task_result);

/**
 * Frees a task handle. Does not cancel the task. If the task is still running, it will continue to run in the background
 */
void ensure_token_task_destory(struct EnsureTokenTask *task);

/**
 * Frees a task result
 */
void ensure_token_task_result_destory(struct EnsureTokenResult *task_result);

/**
 * As client_ensure_token_is_fetched_async but a also gives a fallback url
 * that can be presented to the user in case they accidentally close the automatically
 * opened browser tab or some other mishap happens.
 *
 * If fallback_url is a null pointer, no url will be generated otherwise it is assumed
 * that the pointer points to an FFIString struct. The c_str is expected to be null
 * otherwise an error will be raised. If the c_str field is null, it will be set to
 * a c string containing the url
 *
 * The FFIString struct becomes valid at this point and needs to be destroyed as usual
 */
struct EnsureTokenTask *client_ensure_token_is_fetched_with_fallback_url_async(struct Client *client,
                                                                               struct FFIString *fallback_url);

struct EnsureTokenTask *client_ensure_token_is_fetched_async(struct Client *client);

/**
 * Get the username and user id of user associated with the client (if these exist)
 */
struct OutputMessage *client_get_logged_in_user(struct Client *client);

/**
 * Force a logout of the current user
 */
struct OutputMessage *client_logout_user(struct Client *client);

/**
 * Creates a new build using a set of parameters specified in JSON
 */
struct OutputMessage *client_create_build(struct Client *client,
                                          const uint8_t *json,
                                          uintptr_t length);

/**
 * Creates a new stage using a set of parameters specified in JSON
 */
struct OutputMessage *client_create_stage(struct Client *client,
                                          const uint8_t *json,
                                          uintptr_t length);

/**
 * Gets an abort handle of a task. This may be used to cancel that task
 */
struct AbortHandle *client_upload_task_get_abort_handle(struct UploadTask *task);

/**
 * Waits on the result of an async task. A task can only be waited on once.
 * After this function is called, the pointer to the task will be invalid. If there might be a need to
 * cancel the task use $get_abort_handle_name() to get an abort handle first
 * Produces a task result
 */
struct UploadResult *client_upload_task_wait(struct Client *client,
                                             struct UploadTask *task);

/**
 * Check if the task was cancelled. It is an error to try to get the output from a task that was
 * cancelled so this function must be called before trying to get the output
 */
bool client_upload_task_result_was_cancelled(struct UploadResult *task_result);

/**
 * obtain the output from a task result.
 */
struct OutputMessage *client_upload_task_result_into_output(struct UploadResult *task_result);

/**
 * Frees a task handle. Does not cancel the task. If the task is still running, it will continue to run in the background
 */
void client_upload_task_destory(struct UploadTask *task);

/**
 * Frees a task result
 */
void client_upload_task_result_destory(struct UploadResult *task_result);

/**
 * Uploads files to bard
 */
struct OutputMessage *client_upload(struct Client *client, const uint8_t *json, uintptr_t length);

/**
 * Uploads files to bard
 */
struct UploadTask *client_upload_async(struct Client *client,
                                       const uint8_t *json,
                                       uintptr_t length);

/**
 * Finalizes a stage. If exactly one stage is open for this client, that stage will be finalized, otherwise an error will
 * be returned unless the stage id is specified
 */
struct OutputMessage *client_finalize_stage(struct Client *client,
                                            const uint8_t *json,
                                            uintptr_t length);

/**
 * Finalizes a build. Note that all open stages should be finalized before calling this function
 */
struct OutputMessage *client_finalize_build(struct Client *client,
                                            const uint8_t *json,
                                            uintptr_t length);

/**
 * Takes the progress stream for this client. The client may emit progress update messages as it works
 * especially during uploads. Calling code may wish to monitor progress on a seperate thread and update
 * the user.
 * Note: it is an error to call this function twice.
 */
struct ProgressStream *client_take_progress_stream(struct Client *client);

/**
 * Creates a new portal asset version.
 * asset_line_id - the id of the parent asset line formated as a utf8 string. Note: asset line ids *should* bet just hex strings
 *    so ascii string should also just work here.
 * storage_provider - the storage provider to upload to. May be left null to use the default provider.
 * extension data: JSON encoded extension data. May be null
 */
struct OutputMessage *client_create_portal_asset_version(struct Client *client,
                                                         struct UTF8InputString asset_line_id,
                                                         struct UTF8InputString storage_provider,
                                                         struct UTF8InputString extension_data,
                                                         struct UTF8InputString upload_dir,
                                                         uint64_t chunk_size);

/**
 * Finalizes a new portal asset version.
 * asset_version_id - the id of the asset_version to be finalized, formated as a utf8 string. Note: asset version ids *should* bet just hex strings
 *    so ascii string should also just work here.
 */
struct OutputMessage *client_finalize_portal_asset_version(struct Client *client,
                                                           struct UTF8InputString asset_version_id);

/**
 * Gets an abort handle of a task. This may be used to cancel that task
 */
struct AbortHandle *client_portal_upload_task_get_abort_handle(struct PortalUploadTask *task);

/**
 * Waits on the result of an async task. A task can only be waited on once.
 * After this function is called, the pointer to the task will be invalid. If there might be a need to
 * cancel the task use $get_abort_handle_name() to get an abort handle first
 * Produces a task result
 */
struct PortalUploadResult *client_portal_upload_task_wait(struct Client *client,
                                                          struct PortalUploadTask *task);

/**
 * Check if the task was cancelled. It is an error to try to get the output from a task that was
 * cancelled so this function must be called before trying to get the output
 */
bool client_portal_upload_task_result_was_cancelled(struct PortalUploadResult *task_result);

/**
 * obtain the output from a task result.
 */
struct OutputMessage *client_portal_upload_task_result_into_output(struct PortalUploadResult *task_result);

/**
 * Frees a task handle. Does not cancel the task. If the task is still running, it will continue to run in the background
 */
void client_portal_upload_task_destory(struct PortalUploadTask *task);

/**
 * Frees a task result
 */
void client_portal_upload_task_result_destory(struct PortalUploadResult *task_result);

/**
 * Uploads files to portal.
 */
struct OutputMessage *client_portal_upload(struct Client *client,
                                           struct UTF8InputString asset_version_id,
                                           struct UTF8InputString directory);

/**
 * Async version of [client_portal_upload]
 */
struct UploadTask *client_portal_upload_async(struct Client *client,
                                              struct UTF8InputString asset_version_id,
                                              struct UTF8InputString directory);

/**
 * Creates, uploads file to, and finalizeds a portal asset version all in one step
 * asset_line_id - the id of the parent asset line formated as a utf8 string. Note: asset line ids *should* bet just hex strings
 *    so ascii string should also just work here.
 * storage_provider - the storage provider to upload to. May be left null to use the default provider.
 * directory - the directory where the files may be found
 */
struct OutputMessage *client_upload_portal_asset_version_one_step(struct Client *client,
                                                                  struct OneStepUploadParams params);

/**
 * Gets an abort handle of a task. This may be used to cancel that task
 */
struct AbortHandle *client_upload_one_step_task_get_abort_handle(struct OneStepUploadTask *task);

/**
 * Waits on the result of an async task. A task can only be waited on once.
 * After this function is called, the pointer to the task will be invalid. If there might be a need to
 * cancel the task use $get_abort_handle_name() to get an abort handle first
 * Produces a task result
 */
struct OneStepUploadResult *client_upload_one_step_task_wait(struct Client *client,
                                                             struct OneStepUploadTask *task);

/**
 * Check if the task was cancelled. It is an error to try to get the output from a task that was
 * cancelled so this function must be called before trying to get the output
 */
bool client_upload_one_step_task_result_was_cancelled(struct OneStepUploadResult *task_result);

/**
 * obtain the output from a task result.
 */
struct OutputMessage *client_upload_one_step_task_result_into_output(struct OneStepUploadResult *task_result);

/**
 * Frees a task handle. Does not cancel the task. If the task is still running, it will continue to run in the background
 */
void client_upload_one_step_task_destory(struct OneStepUploadTask *task);

/**
 * Frees a task result
 */
void client_upload_one_step_task_result_destory(struct OneStepUploadResult *task_result);

/**
 * As client_upload_portal_asset_version_one_step but async
 */
struct OneStepUploadTask *client_upload_portal_asset_version_one_step_async(struct Client *client,
                                                                            struct OneStepUploadParams params);

/**
 * Gets details of a specific asset line
 */
struct OutputMessage *client_get_asset_line(struct Client *client,
                                            struct UTF8InputString asset_line_id);

/**
 * Opens the asset line selector gui
 */
struct OutputMessage *open_select_asset_line_gui(struct Client *client,
                                                 struct OpenSelectAssetLineParams params);

/**
 * Gets an abort handle of a task. This may be used to cancel that task
 */
struct AbortHandle *client_open_select_asset_line_gui_get_abort_handle(struct OpenSelectAssetLineGuiTask *task);

/**
 * Waits on the result of an async task. A task can only be waited on once.
 * After this function is called, the pointer to the task will be invalid. If there might be a need to
 * cancel the task use $get_abort_handle_name() to get an abort handle first
 * Produces a task result
 */
struct OpenSelectAssetLineGuiResult *client_open_select_asset_line_gui_wait(struct Client *client,
                                                                            struct OpenSelectAssetLineGuiTask *task);

/**
 * Check if the task was cancelled. It is an error to try to get the output from a task that was
 * cancelled so this function must be called before trying to get the output
 */
bool client_open_select_asset_line_gui_result_was_cancelled(struct OpenSelectAssetLineGuiResult *task_result);

/**
 * obtain the output from a task result.
 */
struct OutputMessage *client_open_select_asset_line_gui_result_into_output(struct OpenSelectAssetLineGuiResult *task_result);

/**
 * Frees a task handle. Does not cancel the task. If the task is still running, it will continue to run in the background
 */
void client_open_select_asset_line_gui_destory(struct OpenSelectAssetLineGuiTask *task);

/**
 * Frees a task result
 */
void client_open_select_asset_line_gui_result_destory(struct OpenSelectAssetLineGuiResult *task_result);

/**
 * As client_upload_portal_asset_version_one_step but async
 */
struct OpenSelectAssetLineGuiTask *client_open_select_asset_line_gui_async(struct Client *client,
                                                                           struct OpenSelectAssetLineParams params);

/**
 * Create a new portal client configuration.
 */
struct Config *config_new(void);

/**
 * Destroy a bard client configuration.
 */
void config_destroy(struct Config *opts);

/**
 * Adds a Central Auth scope to the client configuration. This should generally be "portal.live.access", "portal.dev.access", or "portal.local.access"
 */
bool config_add_scope(struct Config *opts,
                      const char *scope);

/**
 * Override the url of bard. By default, this is `https://bard.zerolight.net`. This option is irrelevant for portal usage
 */
bool config_set_bard_server(struct Config *opts,
                            const char *url);

/**
 * Override the url of portal. By default, this is `https://portaldev.app-platform.zlthunder.net`.
 */
bool config_set_portal_server(struct Config *opts, const char *url);

/**
 * Override the url of the central auth server. By default, this is `https://authentication.zlthunder.net`
 */
bool config_set_zl_central_auth_server(struct Config *opts,
                                       const char *url);

/**
 * Override the path of the file where the central auth token cache is kept.
 */
bool config_set_token_cache_path(struct Config *opts, const char *path);

/**
 * Set a http proxy
 */
bool config_set_http_proxy(struct Config *opts, const char *http_proxy);

void ffi_string_destroy(struct FFIString s);

/**
 * Start logging, calling a callback for each log even emitted.
 * It is safe to call this more than once but any subsequent calls will have no effect.
 * Will fail if called after another method of logging has been selected
 *
 * log_filter - pass a logging filter. May be null to use the default. The format is
 * "debug,foo=info,bar::baz=trace" which set the default to be "debug", the "h2" modules
 * to be "info" and the "bar:baz" module to be at trace
 *
 * MEMORY SAFETY: the c string in the callback will be valid for the duration of the
 * callback. It must be copied to keep it around longer
 */
bool logging_start_callback_logging(void (*callback)(const char*),
                                    struct UTF8InputString log_filter);

/**
 * Blocks waiting for the next progress messsage on this progress stream. Should typically be called
 * from a different thread to the [crate::Client].
 * Note: see the documentation on [GetNextMessageResult] for how to correctly check for errors
 * and the end of messages
 */
struct GetNextMessageResult progress_stream_get_next_message(struct ProgressStream *stream);

/**
 * Destorys a progress stream
 */
void progress_stream_destroy(struct ProgressStream *progress_stream);

/**
 * Destroy an output message
 */
void output_message_destroy(struct OutputMessage *output_message);

/**
 * Gets the kind of a output message. There are an open set of kinds of progress messages,
 * but messages of the same kind should follow the same JSON schema.
 */
struct FFIString output_message_kind(struct OutputMessage *output_message);

/**
 * Formats the progress message into a human readable string suitable for printing to the console or similar
 */
struct FFIString output_message_format_text(struct OutputMessage *output_message);

/**
 * Formats the progress message into a JSON object, suitable for parsing. Messages with the same
 * kind should have the same schema.
 */
struct FFIString output_message_to_json(struct OutputMessage *output_message);

/**
 * *Deprecated* Please use "abort_handle_destroy_name"
 */
void abort_handle_destory_name(struct AbortHandle *abort_handle);

/**
 * Destroy an abort handle without using it. Does not kill the task
 */
void abort_handle_destroy_name(struct AbortHandle *abort_handle);

/**
 * Use an abort handle to kill the associated task. Does not consume the abort handle
 */
bool abort_handle_cancel_task(struct AbortHandle *abort_handle);
