# Usage

## Running

You can run webigeo either

- in the web, at [webigeo.alpinemaps.org](https://webigeo.alpinemaps.org/) using **a browser that supports the [WebGPU API](https://developer.mozilla.org/en-US/docs/Web/API/WebGPU_API)** (for example Google Chrome), or
- natively, by following the [Setup guide](Setup.md#native).

Once the app is running, a GPS track can be loaded and the avalanche simulation can be run for the region around the track.

## Track loading

webigeo supports loading and displaying GPS tracks. The rectangular region (i.e. the axis-aligned bounding box) around the most recently loaded track is used any types of avalanche simulations. After loading a track, the camera is automatically moved such that the entire track is visible from above.

Currently, the only supported track format is [GPX](https://en.wikipedia.org/wiki/GPS_Exchange_Format). GPX is an XML-based format that allows to store a sequence of coordinates with associated properties such as time and elevation. Loaded tracks are displayed by drawing a red line that connects all `<trkpt>` elements in the GPX file ordered by time, regardless of segment or route association. `<wpt>` and `<rte>` elements are ignored.

For now, track loading is the only way of selecting a region for avalanche simulations.

A track can be uploaded in the "Track" section of the UI panel, by clicking "Open GPX File". 

The setting "Line render mode" defines how lines are displayed. The settings are
 - `none`: Tracks are not rendered
 - `without depth test`: Draw lines, even if they are behind terrain
 - `with depth test`: Draw lines only if they are not occluded
 - `semi-transparent`: Draw lines normally if unoccluded and semi-transparently if behind terrain

TODO image of user interface and loaded track

## Simulation

The simulation can be triggered by clicking "Run" in the "Compute pipeline" section of the UI panel. When the settings are changed, a re-run of the simulation triggered. Simulations are always run for the region of the most recently selected track. If no track was selected, the simulation cannot be started.

The "Strength" slider can be used to adjust the opacity of the overlay.

### Input data

The input data for the simulation is the [terrain height as raster data](https://en.wikipedia.org/wiki/Digital_elevation_model) within the selected region. One can switch between either using the DSM (Digital Surface Model, includes trees, buildings, etc.) or DTM (Digital Terrain Model, represents just the ground). For avalanche simulation, generally the DTM is preferred. Additionally, the spatial resolution of the height raster can be adjusted using the "Zoom level" slider. For details about zoom levels, see [here](https://wiki.openstreetmap.org/wiki/Zoom_levels).

### Release points

The simulation starts by identifying possible points, where avalanches may start, so called "release points". Any point with a slope angle between 30 and 60 degrees is considered a release point. This interval may be adjusted in the UI using the sliders for "Release point steepness". Also, the interval of raster cells that are checked for being possible release points can be set using the slider "Release point interval". For example, an interval of 4 means, only every fourth cell of the input height raster is checked for being a release cell.

### Simulation steps

Now avalanche trajectories are computed. An __avalanche trajectory__ is a connected sequence of positions, starting from a release cell. Each trajectory is extended with new positions iteratively. Each iteration is called a __simulation step__. The maximal number of simulation steps can be set using the slider "Num steps".

A __routing model__ dictates how the new position is calculated in each step. We experimented with different routing models, but currently only our default model can be used via the UI. This model works by having multiple trajectories starting at the same point and having each trajectory be slightly offset in random directions each step (our approach is therefore a [Monte-carlo method](https://en.wikipedia.org/wiki/Monte_Carlo_method)). The final result is an aggregation of all individual trajectories. The parameters are

- `Paths per release point`: How many trajectories to start for each point. 
- `Random contribution`: How large the random offset for each step is. A value of zero means no randomness, and only use the local gradient. A larger value results in wider avalanches (if paths per release point is adaquately large).
- `Persistence`: How much "momentum" (i.e. previous directions) contributes to the direction each step. A larger value means straighter avalanches. A value of zero means to follow the local direction (i.e. local gradient with random offset).

Trajectories are not only terminated when the maximum number of simulation steps is reached, but also by an additional condition called __runout model__. A typical runout model looks at the heights along the path and decides when a typical avalanche should actually stop. This is usually much earlier than when the maximum number of simulation steps is exceeded. The runout model can be set using the "Runout model" combo box. Currently, only a single runout model supported, the [FlowPy runout model](https://docs.avaframe.org/en/latest/theoryCom4FlowPy.html). This model only takes a single parameter, an angle $\alpha$. A lower angle angle means longer trajectories. Typical values are between 15 and 35 degrees.

TODO image of user interface

## Example scenario

TODO step-by-step tutorial on how to run simulation for an example file

## Misc setting

TODO is this section needed?

phong shading setting
snow setting