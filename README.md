# Urho3D AsyncLevelLoad

Description
---
Asynchronous level loading sample - demonstrates a two-level system, where you will have at most two levels loaded at a time.  Level loading is achieved by trigger boxes in the scene (green transparent boxes), where the level load information is contained as tags in the nodes, and the loader tracks the current and next level to load.  For demo purpose, these triggers are not in ideal position as level loading should be more inconspicuous instead of popping-in on the screen.


Purpose:
* to avoid load screens when transitioning from level to level.
* to avoid loading all your level assets in memory and only load levels as you need.

Screenshot
---
![alt tag](https://github.com/Lumak/Urho3D-AsyncLevelLoad/blob/master/screenshot/levelscreen.jpg)


To Build
---
To build it, unzip/drop the repository into your Urho3D/ folder and build it the same way as you'd build the default Samples that come with Urho3D.

**Built with Urho3D 1.7 tag.**

License
-----------------------------------------------------------------------------------
The MIT License (MIT)







