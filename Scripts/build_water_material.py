"""Run inside Unreal Editor Python; (re)builds the filtered water material graph.

Usage: pass the target asset as the first argument. An optional second argument
adds an imported normal map for a separate comparison material. An optional
third argument selects one or two world-anchored samples (default one); do not
use either trial on a production target before the bitmap and scene reviews.
The trial roughness-variance default is calibrated from Water 002's source
normal and must be remeasured for a different bitmap.
The graph is plain material data: no Custom HLSL node or runtime scripts. It is the recipe behind
/Game/Water/M_CourseWater and Han's M_Han_Water; see
docs/phase-2/12-realistic-water-refinement-plan.md for the design and gates.
"""
import math
import sys
import unreal as u

TARGET = sys.argv[1] if len(sys.argv) > 1 else '/Game/Water/M_CourseWater'
DETAIL_NORMAL_TEXTURE = sys.argv[2] if len(sys.argv) > 2 else None
DETAIL_SAMPLE_COUNT = int(sys.argv[3]) if len(sys.argv) > 3 else 1
if DETAIL_SAMPLE_COUNT not in (1, 2) or (DETAIL_SAMPLE_COUNT == 2 and not DETAIL_NORMAL_TEXTURE):
    raise ValueError('Detail sample count must be 1 or 2, and 2 requires a detail normal texture')
# Standard's course actor overrides DeepColor to (.03, .20, .35) at runtime.
# Give a separate bitmap trial that same default for a valid actor-free A/B;
# keep the procedural base asset's existing fallback default unchanged.
DEEP_COLOR = ((.018, .065, .105) if 'HanRiver' in TARGET
              else (.03, .20, .35) if DETAIL_NORMAL_TEXTURE else (.025, .10, .18))
G = 9.81
# (wavelength m, direction deg from +X along the river, peak slope). Directions
# spread around the wind axis; slopes stay small so the river reads calm.
WAVES = [(11.0, -35, .018), (6.3, 20, .026), (3.4, 62, .028), (1.7, -12, .026), (.9, 105, .020)]
L = u.MaterialEditingLibrary
MP = u.MaterialProperty

folder, name = TARGET.rsplit('/', 1)
mat = u.load_asset(TARGET) or u.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, u.Material, u.MaterialFactoryNew())
# UE 5.8 asserts when DeleteAllMaterialExpressions touches an expression rooted
# by editor inspection/MCP. Rewire the outputs below; unused old nodes are
# harmless to the compiled shader. Cleanup needs a separately validated workflow.
mat.set_editor_property('tangent_space_normal', False)


def node(cls, x, y, **props):
    e = L.create_material_expression(mat, cls, x, y)
    for key, value in props.items():
        e.set_editor_property(key, value)
    return e


def const(v, x, y):
    return node(u.MaterialExpressionConstant, x, y, r=v)


def binary(cls, a, b, x, y):
    e = node(cls, x, y)
    L.connect_material_expressions(a, '', e, 'A')
    L.connect_material_expressions(b, '', e, 'B')
    return e


def mul(a, b, x, y):
    return binary(u.MaterialExpressionMultiply, a, b, x, y)


def add(a, b, x, y):
    return binary(u.MaterialExpressionAdd, a, b, x, y)


def unary(cls, source, x, y, **props):
    e = node(cls, x, y, **props)
    L.connect_material_expressions(source, '', e, '')
    return e


def power(a, exponent, x, y):
    e = node(u.MaterialExpressionPower, x, y)
    L.connect_material_expressions(a, '', e, 'Base')
    L.connect_material_expressions(const(exponent, x - 180, y + 80), '', e, 'Exp')
    return e


xy = unary(u.MaterialExpressionComponentMask, node(u.MaterialExpressionWorldPosition, -1800, 0), -1600, 0, r=True, g=True, b=False, a=False)
motion = node(u.MaterialExpressionScalarParameter, -1800, 300, parameter_name='MotionScale', default_value=.6, group='Water')
time = node(u.MaterialExpressionTime, -1800, 200)
tscaled = mul(time, motion, -1600, 250)
gx = gy = None
unresolved_variance = None
for i, (lam, deg, slope) in enumerate(WAVES):
    y = 600 + i * 480
    dx, dy = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    freq = math.sqrt(G / (2 * math.pi * lam))  # deep-water dispersion: cycles per second
    k = .01 / lam  # cycles per world unit (cm)
    d = node(u.MaterialExpressionConstant2Vector, -1400, y, r=dx * k, g=dy * k)
    phase = add(binary(u.MaterialExpressionDotProduct, xy, d, -1200, y), mul(tscaled, const(freq, -1400, y + 120), -1200, y + 120), -1000, y)
    wave = unary(u.MaterialExpressionCosine, unary(u.MaterialExpressionFrac, phase, -850, y), -700, y, period=1.0)
    # The phase is still continuous here. Taking derivatives after Frac/Cosine
    # would make cell boundaries appear as a false, enormous pixel footprint.
    ddx = unary(u.MaterialExpressionDDX, phase, -850, y + 160)
    ddy = unary(u.MaterialExpressionDDY, phase, -850, y + 240)
    footprint2 = add(mul(ddx, ddx, -700, y + 160), mul(ddy, ddy, -700, y + 240), -560, y + 220)
    footprint = unary(u.MaterialExpressionSquareRoot, footprint2, -420, y + 220)
    cutoff = add(const(.5, -420, y + 300), mul(footprint, const(-1.0, -420, y + 380), -280, y + 300), -140, y + 300)
    fade_linear = unary(u.MaterialExpressionSaturate, mul(cutoff, const(4.0, -140, y + 380), 0, y + 300), 140, y + 300)
    fade2 = mul(fade_linear, fade_linear, 280, y + 300)
    fade = mul(fade2, add(const(3.0, 280, y + 380), mul(fade_linear, const(-2.0, 280, y + 460), 420, y + 380), 560, y + 300), 700, y + 300)
    # Removed slope energy broadens the specular lobe as the wave disappears.
    retained_energy = mul(fade, fade, 840, y + 380)
    lost = add(const(1.0, 700, y + 380), mul(retained_energy, const(-1.0, 700, y + 460), 840, y + 460), 980, y + 380)
    variance = mul(lost, const(.5 * slope * slope, 980, y + 460), 1120, y + 380)
    unresolved_variance = variance if unresolved_variance is None else add(unresolved_variance, variance, 1260, y + 380)
    term = mul(wave, mul(fade, const(slope, -560, y + 60), -420, y + 100), -280, y)
    tx = mul(term, const(dx, -280, y + 60), -140, y)
    ty = mul(term, const(dy, -280, y + 120), -140, y + 60)
    gx = tx if gx is None else add(gx, tx, 0, y)
    gy = ty if gy is None else add(gy, ty, 0, y + 60)

strength = node(u.MaterialExpressionScalarParameter, 0, 400, parameter_name='NormalStrength', default_value=1.0, group='Water')
neg = mul(strength, const(-1.0, 0, 460), 150, 400)
xy_normal = binary(u.MaterialExpressionAppendVector, mul(gx, neg, 300, 500), mul(gy, neg, 300, 600), 450, 550)
normal = unary(u.MaterialExpressionNormalize, binary(u.MaterialExpressionAppendVector, xy_normal, const(1.0, 450, 650), 600, 550), 750, 550)

if DETAIL_NORMAL_TEXTURE:
    detail_texture = u.load_asset(DETAIL_NORMAL_TEXTURE)
    if not isinstance(detail_texture, u.Texture2D):
        raise ValueError(f'Detail normal must be an imported Texture2D: {DETAIL_NORMAL_TEXTURE}')

    def filtered_detail(scrolling_uv, parameter_name, graph_y, rotate_90=False):
        sample = node(u.MaterialExpressionTextureSampleParameter2D, -1800, graph_y,
                      parameter_name=parameter_name, group='Trial', texture=detail_texture,
                      sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        L.connect_material_expressions(scrolling_uv, '', sample, 'UVs')
        uv_dx = unary(u.MaterialExpressionDDX, scrolling_uv, -1800, graph_y + 400)
        uv_dy = unary(u.MaterialExpressionDDY, scrolling_uv, -1800, graph_y + 500)
        footprint = unary(u.MaterialExpressionSquareRoot,
                          add(binary(u.MaterialExpressionDotProduct, uv_dx, uv_dx, -1600, graph_y + 400),
                              binary(u.MaterialExpressionDotProduct, uv_dy, uv_dy, -1600, graph_y + 500), -1500, graph_y + 450),
                          -1400, graph_y + 400)
        # The visible features are broader than individual texels. Trial a
        # 16-texel feature scale, fading out by 0.5 feature cycles/pixel.
        feature_footprint = mul(footprint, const(64.0, -1400, graph_y + 550), -1200, graph_y + 400)
        cutoff = add(const(.5, -1200, graph_y + 500),
                     mul(feature_footprint, const(-1.0, -1200, graph_y + 600), -1000, graph_y + 500), -900, graph_y + 400)
        fade_linear = unary(u.MaterialExpressionSaturate,
                            mul(cutoff, const(4.0, -900, graph_y + 500), -800, graph_y + 400), -700, graph_y + 400)
        fade = mul(mul(fade_linear, fade_linear, -600, graph_y + 400),
                   add(const(3.0, -600, graph_y + 500),
                       mul(fade_linear, const(-2.0, -600, graph_y + 600), -500, graph_y + 500), -400, graph_y + 500),
                   -300, graph_y + 400)
        if rotate_90:
            # UVs are (world Y, -world X); rotate their sampled slope back
            # into world XY before combining it with the procedural normal.
            slope_u = unary(u.MaterialExpressionComponentMask, sample, -1400, graph_y,
                            r=True, g=False, b=False, a=False)
            slope_v = unary(u.MaterialExpressionComponentMask, sample, -1400, graph_y + 100,
                            r=False, g=True, b=False, a=False)
            detail_xy = binary(u.MaterialExpressionAppendVector,
                               mul(slope_v, const(-1.0, -1200, graph_y + 100), -1000, graph_y + 100),
                               slope_u, -800, graph_y)
        else:
            detail_xy = unary(u.MaterialExpressionComponentMask, sample, -1400, graph_y,
                              r=True, g=True, b=False, a=False)
        retained_energy = mul(fade, fade, -200, graph_y + 300)
        lost_energy = add(const(1.0, -200, graph_y + 400),
                          mul(retained_energy, const(-1.0, -200, graph_y + 500), 0, graph_y + 400),
                          200, graph_y + 400)
        return mul(detail_xy, fade, -200, graph_y + 200), lost_energy

    detail_uv = mul(xy, const(.0033, -2400, 850), -2200, 700)
    detail_flow = mul(tscaled, node(u.MaterialExpressionConstant2Vector, -2400, 1100, r=.015, g=-.007), -2200, 1000)
    first_detail, lost_detail_energy = filtered_detail(
        add(detail_uv, detail_flow, -2000, 900), 'DetailNormalTrial', 700)
    detail_xy = first_detail
    if DETAIL_SAMPLE_COUNT == 2:
        world_x = unary(u.MaterialExpressionComponentMask, xy, -2400, 2600, r=True, g=False, b=False, a=False)
        world_y = unary(u.MaterialExpressionComponentMask, xy, -2400, 2700, r=False, g=True, b=False, a=False)
        rotated_xy = binary(u.MaterialExpressionAppendVector, world_y,
                            mul(world_x, const(-1.0, -2400, 2800), -2200, 2700), -2000, 2600)
        detail_uv_b = mul(rotated_xy, const(.0046, -2000, 2800), -1800, 2600)
        detail_flow_b = mul(tscaled, node(u.MaterialExpressionConstant2Vector, -2000, 2900, r=-.011, g=.009), -1800, 2800)
        second_detail, second_lost_energy = filtered_detail(
            add(detail_uv_b, detail_flow_b, -1600, 2600),
            'DetailNormalTrialB', 2600, rotate_90=True)
        # Keep approximate RMS slope strength comparable to the one-sample
        # trial while the two directions decorrelate the repeated pattern.
        detail_xy = mul(add(first_detail, second_detail, -400, 900),
                        const(1.0 / math.sqrt(2.0), -400, 1000), -200, 900)
        lost_detail_energy = mul(add(lost_detail_energy, second_lost_energy, 0, 1000),
                                 const(.5, 0, 1100), 200, 1000)
    detail_strength = node(u.MaterialExpressionScalarParameter, -1000, 700,
                           parameter_name='DetailStrengthTrial', default_value=.16, group='Trial')
    detail_slope = mul(detail_xy, detail_strength, -600, 700)
    base_xy = unary(u.MaterialExpressionComponentMask, normal, -1000, 400,
                    r=True, g=True, b=False, a=False)
    base_z = unary(u.MaterialExpressionComponentMask, normal, -1000, 500,
                   r=False, g=False, b=True, a=False)
    normal = unary(u.MaterialExpressionNormalize,
                   binary(u.MaterialExpressionAppendVector,
                          add(base_xy, detail_slope, -400, 500), base_z, -200, 500), 0, 500)
    # Water 002 source RGB decodes to mean(XY^2) ~= 0.0578 at full resolution.
    # This trial-only control must be recalibrated if another bitmap is used.
    detail_variance = node(u.MaterialExpressionScalarParameter, 200, 1200,
                           parameter_name='DetailSlopeVarianceTrial',
                           default_value=.0578, group='Trial')
    detail_unresolved_variance = mul(
        mul(lost_detail_energy, detail_variance, 400, 1200),
        power(detail_strength, 2.0, 400, 1300), 600, 1200)
L.connect_material_property(normal, '', MP.MP_NORMAL)

L.connect_material_property(node(u.MaterialExpressionVectorParameter, 750, 0, parameter_name='DeepColor', default_value=u.LinearColor(*DEEP_COLOR, 1), group='Water'), '', MP.MP_BASE_COLOR)
L.connect_material_property(const(0.0, 750, 100), '', MP.MP_METALLIC)
L.connect_material_property(const(.25, 750, 160), '', MP.MP_SPECULAR)  # F0 near .02 for water
base_roughness = node(u.MaterialExpressionScalarParameter, 750, 320, parameter_name='Roughness', default_value=.08, group='Water')
base_alpha2 = power(base_roughness, 4.0, 950, 250)
strength2 = power(strength, 2.0, 950, 350)
microfacet_variance = mul(unresolved_variance, strength2, 1150, 350)
if DETAIL_NORMAL_TEXTURE:
    microfacet_variance = add(microfacet_variance, detail_unresolved_variance, 1250, 450)
filtered_roughness = power(add(base_alpha2, microfacet_variance, 1300, 250), .25, 1450, 250)
roughness = binary(u.MaterialExpressionMin, filtered_roughness, const(.30, 1450, 350), 1600, 250)
L.connect_material_property(roughness, '', MP.MP_ROUGHNESS)

# Restrained sky reflection driven by the animated normal, capped against bright bands.
fresnel = node(u.MaterialExpressionFresnel, 750, 450, exponent=5.0, base_reflect_fraction=.02)
L.connect_material_expressions(normal, '', fresnel, 'Normal')
sky = node(u.MaterialExpressionVectorParameter, 750, 520, parameter_name='SkyTint', default_value=u.LinearColor(.10, .16, .26, 1), group='Water')
cap = node(u.MaterialExpressionScalarParameter, 750, 600, parameter_name='ReflectionMax', default_value=.35, group='Water')
L.connect_material_property(mul(mul(fresnel, sky, 950, 470), cap, 1100, 470), '', MP.MP_EMISSIVE_COLOR)

L.recompile_material(mat)
source = ('Original VIR filtered-wave water material; ' + str(DETAIL_SAMPLE_COUNT) + ' bitmap trial normal sample(s): ' + DETAIL_NORMAL_TEXTURE
          if DETAIL_NORMAL_TEXTURE else 'Original VIR filtered-wave water material; no imported bitmap')
u.EditorAssetLibrary.set_metadata_tag(mat, 'VIR.Source', source)
u.EditorAssetLibrary.save_loaded_asset(mat)
