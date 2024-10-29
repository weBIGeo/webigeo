/*
 *
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

// TODO copyright notice?

#pragma once

#include "params.h"
#include "uniforms.h"
#include "webgpu/webgpu.h"

namespace webgpu_engine::atmosphere {

class SkyAtmosphereRenderer {
    /**
     * Updates the renderer's internal uniform buffer containing the {@link Atmosphere} parameters as well as its host-side copy of {@link Atmosphere}
     * parameters.
     * @param atmosphere The new {@link Atmosphere} to override the current parameters.
     *
     * @see {@link SkyAtmosphereResources.updateAtmosphere}: Updates the host-side {@link Atmosphere} parameters as well as the corresponding uniform
     * buffer.
     */
    virtual void updateAtmosphere(params::Atmosphere atmosphere) = 0;

    /**
     * Updates the renderer's internal uniform buffer containing the {@link Uniforms} as well as its host-side copy of {@link Uniforms}.
     * @param uniforms The new {@link Uniforms} to override the current parameters.
     *
     * If custom uniform buffers are used, this does nothing (see {@link CustomUniformsSourceConfig}).
     *
     * @see {@link SkyAtmosphereResources.updateUniforms}: Update the {@link Uniforms} uniform buffers.
     */
    virtual void updateUniforms(uniforms::Uniforms uniforms) = 0;

    /**
     * Renders the transmittance lookup table.
     *
     * To produce meaningful results, this requires the internal uniform buffer containing the {@link Atmosphere} parameters to hold valid data.
     * Call {@link updateAtmosphere} to ensure this is the case.
     *
     * Since the transmittance lookup table is not view or light souce dependent, this only needs to be called if the {@link Atmosphere} parameters change.
     *
     * @param passEncoder Used to encode rendering of the lookup table. The encoder is not `end()`ed by this function.
     *
     * @see {@link updateAtmosphere}: To write {@link Atmosphere} parameters to the internal uniform buffer, call this function.
     */
    virtual void renderTransmittanceLut(WGPUComputePassEncoder passEncoder) = 0;

    /**
     * Renders the multiple scattering lookup table.
     *
     * To produce meaningful results, this requires the internal uniform buffer containing the {@link Atmosphere} parameters to hold valid data.
     * Call {@link updateAtmosphere} to ensure this is the case.
     *
     * Since the multiple scattering lookup table is not view or light souce dependent, this only needs to be called if the {@link Atmosphere} parameters
     * change.
     *
     * @param passEncoder Used to encode rendering of the lookup table. The encoder is not `end()`ed by this function.
     *
     * @see {@link updateAtmosphere}: To write {@link Atmosphere} parameters to the internal uniform buffer, call this function.
     */
    virtual void renderMultiScatteringLut(WGPUComputePassEncoder passEncoder) = 0;

    /**
     * Renders the transmittance and multiple scattering lookup tables.
     *
     * To produce meaningful results, this requires the internal uniform buffer containing the {@link Atmosphere} parameters to hold valid data.
     * Use the {@link atmosphere} parameter to implicitly update the {@link Atmosphere} parameters or call {@link updateAtmosphere} to ensure this is the
     * case.
     *
     * Since the transmittance and multiple scattering lookup tables are not view or light souce dependent, this only needs to be called if the {@link
     * Atmosphere} parameters change.
     *
     * @param passEncoder Used to encode rendering of the lookup tables. The encoder is not `end()`ed by this function.
     * @param atmosphere If this is defined, {@link updateAtmosphere} is called before rendering the lookup tables.
     *
     * @see {@link updateAtmosphere}: Updates the {@link Atmosphere} parameters.
     * @see {@link renderTransmittanceLut}: Renders the transmittance lookup table.
     * @see {@link renderMultiScatteringLut}: Renders the multiple scattering lookup table.
     */
    virtual void renderConstantLuts(WGPUComputePassEncoder passEncoder) = 0;

    /**
     * Renders the sky view table.
     *
     * To produce meaningful results, this requires the transmittance and multiple scattering lookup tables, as well as the uniform buffers containing the
     * {@link Atmosphere} and {@link Uniforms} parameters to hold valid data. Call {@link renderConstantLuts} and {@link updateUniforms} to ensure this is
     * the case.
     *
     * @param passEncoder Used to encode rendering of the lookup table. The encoder is not `end()`ed by this function.
     *
     * @see {@link renderConstantLuts}: To initialize the transmittance and multiple scattering lookup tables, as well as the internal uniform buffer
     * storing the {@link Atmosphere} parameters, call this function.
     * @see {@link updateUniforms}: To write {@link Uniforms} to the internal uniform buffer, call this function.
     */
    virtual void renderSkyViewLut(WGPUComputePassEncoder passEncoder) = 0;

    /**
     * Renders the aerial perspective lookup table.
     *
     * To produce meaningful results, this requires the transmittance and multiple scattering lookup tables, as well as the uniform buffers containing the
     * {@link Atmosphere} and {@link Uniforms} parameters to hold valid data. Call {@link renderConstantLuts} and {@link updateUniforms} to ensure this is
     * the case.
     *
     * If (a) user-defined shadow map(s) is used (see {@link SkyAtmosphereRendererConfig.shadow}), make sure to encode any updates of the shadow map(s)
     * before encoding this pass.
     *
     * @param passEncoder Used to encode rendering of the lookup table. The encoder is not `end()`ed by this function.
     *
     * @see {@link renderConstantLuts}: To initialize the transmittance and multiple scattering lookup tables, as well as the internal uniform buffer
     * storing the {@link Atmosphere} parameters, call this function.
     * @see {@link updateUniforms}: To write {@link Uniforms} to the internal uniform buffer, call this function.
     */
    virtual void renderAerialPerspectiveLut(WGPUComputePassEncoder passEncoder) = 0;

    /**
     * Renders the sky view and aerial perspective lookup tables.
     *
     * To produce meaningful results, this requires the transmittance and multiple scattering lookup tables, as well as the uniform buffers containing the
     * {@link Atmosphere} and {@link Uniforms} parameters to hold valid data. Call {@link renderConstantLuts} and {@link updateUniforms} to ensure this is
     * the case.
     *
     * If (a) user-defined shadow map(s) is used (see {@link SkyAtmosphereRendererConfig.shadow}), make sure to encode any updates of the shadow map(s)
     * before encoding this pass.
     *
     * @param passEncoder Used to encode rendering of the lookup tables. The encoder is not `end()`ed by this function.
     * @param uniforms If this is defined, {@link updateUniforms} is called before rendering the lookup tables.
     *
     * @see {@link renderConstantLuts}: To initialize the transmittance and multiple scattering lookup tables, as well as the internal uniform buffer
     * storing the {@link Atmosphere} parameters, call this function.
     * @see {@link updateUniforms}: Updates the internal {@link Uniforms} uniform buffer.
     * @see {@link renderSkyViewLut}: Renders the sky view lookup table.
     * @see {@link renderAerialPerspectiveLut}: Renders the aerial perspective lookup table.
     */
    virtual void renderDynamicLuts(WGPUComputePassEncoder passEncoder, uniforms::Uniforms uniforms) = 0;

    /**
     * Renders the lookup tables required for rendering the sky / atmosphere.
     *
     * To initialize or update the transmittance and multiple scattering lookup tables, pass new {@link Atmosphere} paramters to this function or use the
     * `forceConstantLutRendering` parameter.
     *
     * @param passEncoder A `GPUComputePassEncoder` to encode passes with. The encoder is not `end()`ed by this function.
     * @param uniforms {@link Uniforms} to use for this frame. If this is given, the internal uniform buffer will be updated using {@link updateUniforms}.
     * @param atmosphere {@link Atmosphere} parameters to use for this frame. If this is given, the internal uniform buffer storing the {@link Atmosphere}
     * parameters will be updated and the transmittance and multiple scattering lookup tables will be rendered.
     * @param skipDynamicLutRendering If this is true, the sky view and aerial perspective lookup tables will not be rendered.
     * @param forceConstantLutRendering If this is true, the transmittance and multiple scattering lookup tables will be rendered regardless of whether the
     * `atmosphere` parameter is `undefined` or not.
     * @param forceSkyViewLutRendering If this is true, the sky view lookup table will be rendered, even if {@link skipDynamicLutRendering} is true.
     * Defaults to false.
     *
     * @see {@link renderConstantLuts}: Renders the lookup tables that are constant for a given {@link Atmosphere}.
     * @see {@link updateUniforms}: Updates the internal {@link Uniforms} uniform buffer.
     * @see {@link renderDynamicLuts}: Renders the view-dependent lookup tables.
     * @see {@link renderSkyViewLut}: Renders the sky view lookup table.
     */
    virtual void renderLuts(WGPUComputePassEncoder passEncoder, bool skipDynamicLutRendering, bool forceConstantLutRendering, bool forceSkyViewLutRendering)
        = 0;
};

} // namespace webgpu_engine::atmosphere
