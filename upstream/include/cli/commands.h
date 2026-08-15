#ifndef GSR_CLI_COMMANDS_H
#define GSR_CLI_COMMANDS_H

/* These are the |args_handlers| of the argument parser. They return the exit code that gpu-screen-recorder should exit with */
int version_command(void *userdata);
int info_command(void *userdata);
int list_audio_devices_command(void *userdata);
int list_application_audio_command(void *userdata);
int list_v4l2_devices(void *userdata);
int list_capture_options_command(const char *card_path, void *userdata);
int list_monitors_command(void *userdata);

void run_recording_saved_script_async(const char *script_file, const char *video_file, const char *type);

#endif /* GSR_CLI_COMMANDS_H */
