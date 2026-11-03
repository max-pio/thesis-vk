# Volume Rendering

Loading 3D scalar fields (CT, MRI, simulations), the volume sample, sources of data and literature. Back to the [README](../README.md).

## Contents

* [Loading Volumes](#loading-volumes)
* [Volume Sample](#volume-sample)
* [Default Data Sets](#default-data-sets)
* [Other Sources of Data](#other-sources-of-data)
* [Converting Data](#converting-data)
* [Papers to Compare With](#papers-to-compare-with)
* [Literature](#literature)

## Loading Volumes

`utils::loadVolume(path)` (`core/volume.hpp`) reads raw files: the voxels without header, little-endian, x fastest, then y, then z. Size and type are part of the file name:

* `name_WxHxD_type.raw`, e.g. `bonsai_256x256x256_uint8.raw`, with type `uint8`, `uint16`, `int16`, `float32` or `float64`. GPUs cannot filter 64 bit floats, so `float64` is converted to `float32` while loading (with a warning).
* `name_WxHxD.raw`: the type is inferred from the file size (1 byte: `uint8`, 2: `uint16`, 4: `float32`, 8: `float64`). Signed 16 bit data needs the explicit `int16`.

```cpp
Volume volume = utils::loadVolume(path);   // throws std::runtime_error if name or size do not fit
Image texture = context.createTexture(VK_IMAGE_TYPE_3D, .., utils::getVkFormat(volume.type), ..);
```

* Integer voxels are normalized by the texture format (`uint8` and `uint16` to [0, 1], `int16` to [-1, 1]), so shaders always sample floats. `Volume::minValue` / `maxValue` give the range in these units.
* The raw format has no voxel spacing: `Volume::spacing` is 1 after loading, set it yourself (see the table below).
* The whole file is read into memory and the 3D texture needs the same amount of GPU memory. Volumes of several GB need streaming or bricking.

## Volume Sample

`sample-volume` (`samples/volume/`) loads a volume into a 3D texture and ray marches it in a fragment shader.

```
cmake -P cmake/download_volumes.cmake        # once, downloads bonsai
./build-debug/sample-volume
./build-debug/sample-volume --data data/volumes/carp_256x256x512_uint16.raw
```

* The ray is intersected with the bounding box and sampled in steps with a random start offset per pixel.
* Modes: direct volume rendering (emission-absorption, front-to-back compositing with early termination), and isosurface with gradient shading.
* The GUI sets value window, density, iso value, sampling rate and voxel spacing.
* The x, y and z axes of the file map to the world axes with +y up. CT and MRI scans usually have the long body axis along z and appear rotated (`mri_woman` is seen from above).
* Not included: transfer function editor and lighting beyond the isosurface headlight.

## Default Data Sets

`cmake/download_volumes.cmake` downloads data sets of the [Open SciVis Datasets](https://klacansky.com/open-scivis-datasets/) collection (Pavol Klacansky) into `data/volumes/`, which git ignores, and checks the SHA-512 hashes from the web page.

```
cmake -P cmake/download_volumes.cmake                         # bonsai
cmake -DVOLUMES="neghip;foot;carp" -P cmake/download_volumes.cmake
cmake -DVOLUMES=all -P cmake/download_volumes.cmake
cmake -DVOLUMES=list -P cmake/download_volumes.cmake          # lists the names
```

The collection has no single license and the data sets have different origins. Check the acknowledgement on the web page and cite the data set (BibTeX is provided there) if you use it in your thesis. Sizes are width x height x depth.

| Name | Size | Type | Spacing | File size | Notes |
|---|---|---|---|---|---|
| `neghip` | 64x64x64 | uint8 | 1x1x1 | 256 kB | tiny, for tests |
| `bonsai` (default) | 256x256x256 | uint8 | 1x1x1 | 16 MB | CT, thin structures |
| `foot`, `skull`, `aneurism` | 256x256x256 | uint8 | 1x1x1 | 16 MB each | Foot in the visibility optimization paper |
| `csafe_heptane` | 302x302x302 | uint8 | 1x1x1 | 26 MB | simulation, Heptane in the visibility optimization paper |
| `mri_woman` | 256x256x109 | uint16 | 1x1x1.5 | 14 MB | MRI, uses only 12 of 16 bit |
| `tacc_turbulence` | 256x256x256 | float32 | 1x1x1 | 64 MB | simulation, heavy-tailed values: narrow the window |
| `carp` | 256x256x512 | uint16 | 0.78x0.39x1 | 64 MB | CT, anisotropic voxels |
| `duct` | 193x194x1000 | float32 | 1x1x1 | 143 MB | simulation |
| `bunny` | 512x512x361 | uint16 | 0.34x0.34x0.5 | 181 MB | CT |
| `prone` | 512x512x463 | uint16 | 0.625x0.625x1 | 232 MB | CT (colon) |
| `magnetic_reconnection` | 512x512x512 | float32 | 1x1x1 | 512 MB | simulation, in the visibility optimization paper |
| `chameleon` | 1024x1024x1080 | uint16 | 0.092x0.092x0.105 | 2.1 GB | CT, in both papers below |

The web page lists larger volumes (up to 256 GB).

## Other Sources of Data

Check the license and terms of use of each data set and cite it. Medical data often requires an account or an agreement and must not be redistributed, so do not commit volumes to the repository.

| Source | Content | Access |
|---|---|---|
| [OsiriX DICOM Image Library](https://www.osirix-viewer.com/resources/dicom-image-library/) | anonymized clinical CT / MR / PET studies as DICOM, including Manix | free login |
| [Stanford Volume Data Archive](https://graphics.stanford.edu/data/voldata/) | classic CT and MRI data sets | download |
| [volvis.org](http://www.volvis.org/) | older collection, the origin of many Open SciVis volumes | download |
| [The Cancer Imaging Archive](https://www.cancerimagingarchive.net/) | CT / MR studies as DICOM | terms per collection |
| [Visible Human Project](https://www.nlm.nih.gov/research/visible/visible_human.html) | cryosection, CT and MR data of two bodies | license agreement |
| [DigiMorph](https://digimorph.org/), [MorphoSource](https://morphosource.org/) | micro-CT scans of animals and fossils | per specimen |
| [IEEE SciVis Contest](https://sciviscontest.ieeevis.org/) | simulation data sets of the yearly contests | per contest |
| [Johns Hopkins Turbulence Databases](https://turbulence.pha.jhu.edu/) | DNS turbulence fields | free, token for bulk access |

## Converting Data

Other formats have to be converted to raw files. DICOM series can be read with [SimpleITK](https://simpleitk.org/); NRRD, NIfTI and MetaImage with `sitk.ReadImage(path)`. [3D Slicer](https://www.slicer.org/) can export many formats.

```python
import SimpleITK as sitk
reader = sitk.ImageSeriesReader()
reader.SetFileNames(reader.GetGDCMSeriesFileNames("dicom_dir"))
image = reader.Execute()
voxels = sitk.GetArrayFromImage(image)       # shape (z, y, x)
z, y, x = voxels.shape
print(image.GetSpacing())                    # voxel spacing in mm
voxels.astype("<i2").tofile(f"name_{x}x{y}x{z}_int16.raw")
```

CT values are Hounsfield units (about -1024 to 3071) and fit `int16`. Anisotropic voxels (`carp`, `bunny`, most CT data) look squashed unless the bounding box is scaled by the spacing, which `sample-volume` does with its voxel spacing setting.

## Papers to Compare With

* [View-Dependent Visibility Optimization for Monte Carlo Volume Visualization](https://cg.ivd.kit.edu/publications/2025/mc-visibility-opt/paper_mc-vis-opt.pdf) (Lerzer and Dachsbacher, Eurographics 2025): Chameleon, Heptane, Foot and Magnetic Reconnection.
* [Real-Time Denoising of Volumetric Path Tracing for Direct Volume Rendering](https://arxiv.org/pdf/2106.08034) (Iglesias-Guitian et al.): Chameleon, Manix, Heloderma and Dragon. It works with G-buffers.

## Literature

* Engel, Hadwiger, Kniss, Rezk-Salama, Weiskopf: *Real-Time Volume Graphics*, A K Peters, 2006.
* Levoy: *Display of Surfaces from Volume Data*, IEEE Computer Graphics and Applications, 1988.
* Max: *Optical Models for Direct Volume Rendering*, IEEE TVCG, 1995.
* Ljung, Krüger, Gröller, Hadwiger, Hansen, Ynnerman: *State of the Art in Transfer Functions for Direct Volume Rendering*, Computer Graphics Forum, 2016.
* Novák, Georgiev, Hanika, Jarosz: *Monte Carlo Methods for Volumetric Light Transport Simulation*, Computer Graphics Forum (Eurographics STAR), 2018.
