# srt2dvbsub To-Do list (additional, nice to have features)

1. Add a cli flag and logic to enable the user to specify the PIDs of the dvb subtitle tracks. The current bahaviour is simply to add the subtitle tracks after the last audio track found in the input mpeg-ts file.
```
    Example:
    (--pid 150,[151]) 
```


2. Add a cli flag and logic to override the automatic mpeg-ts muxer bitrate calculation. At the moment it automagically calculates the muxrate and if it is not enough to add the subtitle track(s), it will automaically add some stuffing (increases the overall output bitrate)
```
    Example:
    (--ts-bitrate 12000000)
```


3. Add a cli flag and logic to enable generation of PNG files without enabling the "--debug" flag. At the moment, png generation only happens when the debug flag is set > 2 but and this outputs complete debug information to stderr. The output directory logic is already implemented but when flag is set for png only generation a directory must be specified.
```
    Example:
    (--png-only --png-dir "./pngs")