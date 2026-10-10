# Changelog

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
