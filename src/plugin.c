#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ts3_functions.h"

#include "plugin.h"
#include "teamspeak/public_errors.h"

static struct TS3Functions ts3Functions;

#define PLUGIN_API_VERSION 26

#define PATH_BUFSIZE 512

static char* pluginID = NULL;

char pluginDirPath[PATH_BUFSIZE];
char fifoPath[PATH_BUFSIZE];
pthread_t reader_thread = -1;

#define PLUGIN_NAME "pipe_plugin"

/*********************************** Required functions ************************************/
/*
 * If any of these required functions is not implemented, TS3 will refuse to load the plugin
 */

/* Unique name identifying this plugin */
const char* ts3plugin_name()
{
    return PLUGIN_NAME;
}

/* Plugin version */
const char* ts3plugin_version()
{
    return "1.0";
}

/* Plugin API version. Must be the same as the clients API major version, else the plugin fails to load. */
int ts3plugin_apiVersion()
{
    return PLUGIN_API_VERSION;
}

/* Plugin author */
const char* ts3plugin_author()
{
    /* If you want to use wchar_t, see ts3plugin_name() on how to use */
    return "Phillip Schichtel, schich.tel";
}

/* Plugin description */
const char* ts3plugin_description()
{
    /* If you want to use wchar_t, see ts3plugin_name() on how to use */
    return "This plugin reads actions from a named pipe/fifo.";
}

/* Set TeamSpeak 3 callback functions */
void ts3plugin_setFunctionPointers(const struct TS3Functions funcs)
{
    ts3Functions = funcs;
}

void vlog_message(enum LogLevel level, char* format, va_list args) {
    char logBuffer[300];
    int result = vsnprintf(logBuffer, sizeof(logBuffer), format, args);
    if (result < 0) {
        return;
    }
    pthread_t tid = pthread_self();
    ts3Functions.logMessage(logBuffer, level, PLUGIN_NAME, tid);
}

void log_message(enum LogLevel level, char* format, ...) {
    va_list args;
    va_start(args, format);
    vlog_message(level, format, args);
    va_end(args);
}

void log_error(char* format, ...) {
    va_list args;
    va_start(args, format);
    vlog_message(LogLevel_ERROR, format, args);
    va_end(args);
}

void toggle_state(uint64 handler, enum ClientProperties prop, int active, int inactive) {
    int value;
    if (ts3Functions.getClientSelfVariableAsInt(handler, prop, &value) != ERROR_ok) {
        log_error("Failed to get state!");
        return;
    }
    int newValue = (value == active) ? inactive : active;
    if (ts3Functions.setClientSelfVariableAsInt(handler, prop, newValue) != ERROR_ok) {
        log_error("Failed to set state!");
        return;
    }
}

void flush_changes(uint64 handler) {
    ts3Functions.flushClientSelfUpdates(handler, NULL);
}

void toggle_microphone(uint64 handler) {
    toggle_state(handler, CLIENT_INPUT_MUTED, MUTEINPUT_NONE, MUTEINPUT_MUTED);
    flush_changes(handler);
}

void toggle_speaker(uint64 handler) {
    toggle_state(handler, CLIENT_OUTPUT_MUTED, MUTEOUTPUT_NONE, MUTEOUTPUT_MUTED);
    flush_changes(handler);
}

int for_each_server(void (*f)(uint64 handler)) {
    uint64* handlers;
    if (ts3Functions.getServerConnectionHandlerList(&handlers)) {
        return 1;
    }
    for (int i = 0; handlers[i]; ++i) {
        f(handlers[i]);
    }

    return 0;
}

char* trim(char* str)
{
    // Trim leading space
    while (isspace((unsigned char) *str)) {
        str++;
    }

    // All spaces?
    if(*str == 0) {
        return str;
    }

    // Trim trailing space
    char *end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char) *end)) {
        end--;
    }

    // Write new null terminator character
    end[1] = '\0';

    return str;
}

void perform_action(char* rawAction) {
    char* trimmedAction = trim(rawAction);

    if (strcmp("toggle_speaker", trimmedAction) == 0) {
        for_each_server(toggle_speaker);
    } else if (strcmp("toggle_microphone", trimmedAction) == 0) {
        for_each_server(toggle_microphone);
    } else {
        log_message(LogLevel_WARNING, "Unknown action: %s", trimmedAction);
    }
}

void* read_fifo(void* arg) {
    char buffer[512];
    size_t bytesRead;
    int fifoHandle;

    while (true) {
        pthread_testcancel();

        fifoHandle = open(fifoPath, O_RDONLY);
        if (fifoHandle < 0) {
            char* err = strerror(errno);
            log_error("Failed to open FIFO (%s), sleeping...", err);
            sleep(2);
            continue;
        }

        log_message(LogLevel_DEBUG, "Waiting for input from fifo...");
        bytesRead = read(fifoHandle, buffer, sizeof(buffer) - 1);
        close(fifoHandle);
        if (bytesRead == -1) {
            log_error("Failed to read from fifo: %s", strerror(errno));
            continue;
        }
        buffer[bytesRead] = '\0';

        log_message(LogLevel_INFO, "Received %lu bytes via fifo", bytesRead);
        perform_action(buffer);
    }
}

bool file_exists(char* path) {
    struct stat buffer;
    return !stat(path, &buffer);
}

bool is_fifo(char *path) {
    struct stat buffer;
    if (stat(path, &buffer)) {
        return false;
    }
    return S_ISFIFO(buffer.st_mode);
}

/*
 * Custom code called right after loading the plugin. Returns 0 on success, 1 on failure.
 * If the function returns 1 on failure, the plugin will be unloaded again.
 */
int ts3plugin_init()
{
    char pluginPath[PATH_BUFSIZE];

    log_message(LogLevel_INFO, "Starting...");

    /* Example on how to query application, resources and configuration paths from client */
    /* Note: Console client returns empty string for app and resources path */
    ts3Functions.getPluginPath(pluginPath, PATH_BUFSIZE, pluginID);

    if (snprintf(pluginDirPath, sizeof(pluginDirPath), "%s/%s", pluginPath, PLUGIN_NAME) < 0) {
        return 1;
    }
    if (snprintf(fifoPath, sizeof(fifoPath), "%s/%s", pluginDirPath, "commands.pipe") < 0) {
        return 1;
    }


    if (!file_exists(pluginDirPath)) {
        if (mkdir(pluginDirPath, 0755)) {
            log_error("Failed to setup plugin directory: %s!", strerror(errno));
            return 1;
        }
    }
    if (!file_exists(fifoPath)) {
        if (mkfifo(fifoPath, 0644)) {
            log_error("Failed to setup fifo: %s!", strerror(errno));
            return 1;
        }
    } else if (!is_fifo(fifoPath)) {
        log_error("Fifo file exists at %s, but is not a fifo!", fifoPath);
        return 1;
    }

    int create_result = pthread_create(&reader_thread, NULL, read_fifo, NULL);
    if (create_result) {
        log_error("Failed to start pipe reader: %s!", strerror(create_result));
        reader_thread = -1;
        return 1;
    }

    return 0; /* 0 = success, 1 = failure, -2 = failure but client will not show a "failed to load" warning */
              /* -2 is a very special case and should only be used if a plugin displays a dialog (e.g. overlay) asking the user to disable
	 * the plugin again, avoiding the show another dialog by the client telling the user the plugin failed to load.
	 * For normal case, if a plugin really failed to load because of an error, the correct return value is 1. */
}

/* Custom code called right before the plugin is unloaded */
void ts3plugin_shutdown()
{
    log_message(LogLevel_INFO, "Shutting down...");

    if (reader_thread != -1) {
        pthread_cancel(reader_thread);
        void* result;
        pthread_join(reader_thread, &result);
    }

    /*
	 * Note:
	 * If your plugin implements a settings dialog, it must be closed and deleted here, else the
	 * TeamSpeak client will most likely crash (DLL removed but dialog from DLL code still open).
	 */

    /* Free pluginID if we registered it */
    if (pluginID) {
        free(pluginID);
        pluginID = NULL;
    }
}

/****************************** Optional functions ********************************/
int ts3plugin_offersConfigure()
{
    return PLUGIN_OFFERS_NO_CONFIGURE;
}

/*
 * If the plugin wants to use error return codes, plugin commands, hotkeys or menu items, it needs to register a command ID. This function will be
 * automatically called after the plugin was initialized. This function is optional. If you don't use these features, this function can be omitted.
 * Note the passed pluginID parameter is no longer valid after calling this function, so you must copy it and store it in the plugin.
 */
void ts3plugin_registerPluginID(const char* id)
{
    const size_t sz = strlen(id) + 1;
    pluginID        = (char*)malloc(sz * sizeof(char));
    strncpy(pluginID, id, sz - 1);
    pluginID[sz - 1] = '\0';
}

int ts3plugin_requestAutoload()
{
    return 0;
}
