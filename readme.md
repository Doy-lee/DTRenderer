# DTRenderer
DTRenderer is an attempt to build a simple software renderer from the ground up. The main goal is to build an intuitive understanding of graphics mathematics, rendering pipeline and core concepts. It runs as a Win32 program with minimal dependencies.

![DTRenderer Demo](docs/20170609_dtrenderer_demo.gif)

The following resouces have been largely informative to my understanding of the software renderer,
* [Handmade Hero](http://handmadehero.org/)
* [Optimising Software Occclusion Articles](https://fgiesen.wordpress.com/2013/02/17/optimizing-sw-occlusion-culling-index/)
* [TinyRenderer](https://github.com/ssloy/tinyrenderer)

## Building
DTRenderer is a Win32 program and can be compiled with **build.bat** which requires Microsoft Visual Studio **cl.exe** to be in the Windows path. It is originally built using Visual Studio 2017, older versions are still somewhat compatible, albeit there are some switches which have been renamed in the 2017 edition.

The Visual Studio solution file is provided as a thin wrapper for interfacing with the Visual Studio debugger and **can not be used to compile.**

## Milestones
* 2D Software Rendering
  * Alpha Blending
  * Bilinear Filtering
  * Correct Color Space Pipeline _(SRGB <-> Linear)_
  * Rasterisation
  * Texture Mapping
  * Translation, Rotation, Scale
* 3D Model Rendering
  * Full Bright, Flat, Gouraud Lighting
  * Orthographic, Perspective Projections
  * Translation, Rotation, Scale
* Custom Wavefront Obj Loader _(minimal subset)_
* Hot-Reloadable DLL for Renderer Code _(taken from Handmade Hero)_
* SIMD "Optimisation" _(with a grain of salt, tried to with some % improvements)_

## Libraries
The external libraries that have been used,
* STB Single File Libraries
  * STB Image _(for image loading)_
  * STB TrueType (and RectPack) _(for creating bitmap font)_

### Debugging Libraries
These libraries are non-essential and used for validating results and debugging the program.
  * STB ImageWrite _(for debugging renders, TrueType output etc.)_
* EasyProfiler _(for profiling)_
* TinyRenderer _(for comparing data output for validity of results)_
