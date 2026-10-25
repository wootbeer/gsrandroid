# Changelog

## [Unreleased]

### Fixed
- The Lost Age: with the CPU renderer, Expanded view now fills the screen with Stretch / Zoom (shows the normal picture; the wider view needs the GPU renderer)  

## [0.4. (10-24-2026)]

### Added
- Golden Sun: The Lost Age support (same app; pick its ROM in the launcher)  
- Transfer your Golden Sun party to The Lost Age: "Load a Golden Sun save file?" on a new game uses this app's Golden Sun save or a .sav file you pick  
- Seamless mode: finishing Golden Sun continues straight into The Lost Age (asked on a new Golden Sun game when The Lost Age is installed)  
- Saves page in the menu: export and import .sav files (GBA emulator / flash cart saves), restore the save an import replaced, back up both games' saves to a folder  

### Changed
- Bumped to the latest GSRecomp version (0.6)  
- The game code is rebuilt once on first start after updating  
- Renderer: Auto uses the GPU renderer for The Lost Age in Expanded view  

## [0.3. (10-10-2026)]

### Changed
- Bumped to the latest GSRecomp version (0.5.3)  
- The game code is rebuilt once on first start after updating  
- Renderer: Auto now uses the CPU renderer (fast enough for Expanded view now); GPU is marked experimental  

### Fixed
- First-time build could freeze at 59% and never finish  
- The build log (Android/data/wootbeer.gsrandroid/files/gsr_build_log.txt) was never written  
- Expanded view ran slowly (now about as fast as Native)  
- Title and file select looked blurry in Expanded view  
- Screen tearing (a line across the picture), most visible when fast forwarding  
- Garbled colours beside the status screen in Expanded view  
- Black strip at the top of the sides in battles in Expanded view  

## [0.2. (10-9-2026)]

### Added
- Save states  
- Android version of Screen filters 
- Touch pad option: Joystick or D-pad  
- Change ROM option. Your save is kept  
- New screen colors from GSRecomp: Handheld, Handheld (lighter), Soft, Natural, Warm and Deep  

### Changed
- Bumped to the latest GSRecomp version (0.4.3)  
- ROM loader tidy up  

### Fixed
- First-time build could stop at 59% on slower devices  
- Stereo sound fixed  

## [0.1. (10-6-2026)]

First public release.  
This is an extreme beta-build.  

### Added
- Android support from Descore  
- ROM selection screen  
- On-device ROM / asset processing. The APK contains no Golden Sun code or assets  
- Renderer settings: Auto, CPU or GPU  
- Side menu  
- Display, audio, speed and touch options  
- View mode, scaling, filter, flicker reduction, frame interpolation  
- Touch control size and opacity  
- On-screen touch controls and physical controller support with button remapping  
- Local crash logging  

### Changed
- Video settings apply instantly  
- Moved cheats to side menu  

### Fixed
- Intro lens flare flicker. Sort-of, only proper on an LCD  
- Audio slowdown when the GPU renderer couldn't keep pace (handled by Auto falling back to CPU)  

### Removed
- Redundant options from in-game menu
