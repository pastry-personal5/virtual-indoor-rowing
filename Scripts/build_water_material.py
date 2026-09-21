"""Run inside Unreal Editor Python; (re)builds the math-only water material graph.

Usage: set TARGET below, or pass it as the first entry of sys.argv. The graph is
plain material data: no textures, no Custom HLSL node, no scripts. It is the
recipe behind /Game/Water/M_CourseWater and Han's M_Han_Water; see
docs/phase-2/07-milestone-3-enhanced-water.md for the design and its limits.
"""
import math
import sys
import unreal as u

TARGET = sys.argv[1] if len(sys.argv) > 1 else '/Game/Water/M_CourseWater'
DEEP_COLOR = (.018, .065, .105) if 'HanRiver' in TARGET else (.025, .10, .18)
G = 9.81
# (wavelength m, direction deg from +X along the river, peak slope). Directions
# spread around the wind axis; slopes stay small so the river reads calm.
WAVES = [(11.0, -35, .018), (6.3, 20, .026), (3.4, 62, .028), (1.7, -12, .026), (.9, 105, .020)]
L = u.MaterialEditingLibrary
MP = u.MaterialProperty

folder, name = TARGET.rsplit('/', 1)
mat = u.load_asset(TARGET) or u.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, u.Material, u.MaterialFactoryNew())
L.delete_all_material_expressions(mat)
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


def unary(cls, a, x, y, **props):
    e = node(cls, x, y, **props)
    L.connect_material_expressions(a, '', e, '')
    return e


xy = unary(u.MaterialExpressionComponentMask, node(u.MaterialExpressionWorldPosition, -1800, 0), -1600, 0, r=True, g=True, b=False, a=False)
motion = node(u.MaterialExpressionScalarParameter, -1800, 300, parameter_name='MotionScale', default_value=.6, group='Water')
tscaled = mul(node(u.MaterialExpressionTime, -1800, 200), motion, -1600, 250)
depth = node(u.MaterialExpressionPixelDepth, -1800, 450)

gx = gy = None
for i, (lam, deg, slope) in enumerate(WAVES):
    y = 600 + i * 300
    dx, dy = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    freq = math.sqrt(G / (2 * math.pi * lam))  # deep-water dispersion: cycles per second
    k = .01 / lam  # cycles per world unit (cm)
    d = node(u.MaterialExpressionConstant2Vector, -1400, y, r=dx * k, g=dy * k)
    phase = add(binary(u.MaterialExpressionDotProduct, xy, d, -1200, y), mul(tscaled, const(freq, -1400, y + 120), -1200, y + 120), -1000, y)
    wave = unary(u.MaterialExpressionCosine, unary(u.MaterialExpressionFrac, phase, -850, y), -700, y, period=1.0)
    # Fade a wave out before its wavelength becomes unresolvable, or it shimmers.
    fade = unary(u.MaterialExpressionSaturate, unary(u.MaterialExpressionOneMinus, mul(depth, const(.01 / (lam * 45.0), -1000, y + 160), -850, y + 160), -700, y + 160), -560, y + 160)
    term = mul(wave, mul(fade, const(slope, -560, y + 60), -420, y + 100), -280, y)
    tx = mul(term, const(dx, -280, y + 60), -140, y)
    ty = mul(term, const(dy, -280, y + 120), -140, y + 60)
    gx = tx if gx is None else add(gx, tx, 0, y)
    gy = ty if gy is None else add(gy, ty, 0, y + 60)

strength = node(u.MaterialExpressionScalarParameter, 0, 400, parameter_name='NormalStrength', default_value=1.0, group='Water')
neg = mul(strength, const(-1.0, 0, 460), 150, 400)
xy_normal = binary(u.MaterialExpressionAppendVector, mul(gx, neg, 300, 500), mul(gy, neg, 300, 600), 450, 550)
normal = unary(u.MaterialExpressionNormalize, binary(u.MaterialExpressionAppendVector, xy_normal, const(1.0, 450, 650), 600, 550), 750, 550)
L.connect_material_property(normal, '', MP.MP_NORMAL)

L.connect_material_property(node(u.MaterialExpressionVectorParameter, 750, 0, parameter_name='DeepColor', default_value=u.LinearColor(*DEEP_COLOR, 1), group='Water'), '', MP.MP_BASE_COLOR)
L.connect_material_property(const(0.0, 750, 100), '', MP.MP_METALLIC)
L.connect_material_property(const(.25, 750, 160), '', MP.MP_SPECULAR)  # F0 near .02 for water
roughness = node(u.MaterialExpressionLinearInterpolate, 1050, 250)
L.connect_material_expressions(node(u.MaterialExpressionScalarParameter, 750, 320, parameter_name='Roughness', default_value=.08, group='Water'), '', roughness, 'A')
L.connect_material_expressions(const(.30, 750, 250), '', roughness, 'B')
L.connect_material_expressions(unary(u.MaterialExpressionSaturate, mul(depth, const(1 / 12000.0, 750, 380), 800, 250), 900, 250), '', roughness, 'Alpha')
L.connect_material_property(roughness, '', MP.MP_ROUGHNESS)  # lost small waves become roughness

# Restrained sky reflection driven by the animated normal, capped against bright bands.
fresnel = node(u.MaterialExpressionFresnel, 750, 450, exponent=5.0, base_reflect_fraction=.02)
L.connect_material_expressions(normal, '', fresnel, 'Normal')
sky = node(u.MaterialExpressionVectorParameter, 750, 520, parameter_name='SkyTint', default_value=u.LinearColor(.10, .16, .26, 1), group='Water')
cap = node(u.MaterialExpressionScalarParameter, 750, 600, parameter_name='ReflectionMax', default_value=.35, group='Water')
L.connect_material_property(mul(mul(fresnel, sky, 950, 470), cap, 1100, 470), '', MP.MP_EMISSIVE_COLOR)

L.recompile_material(mat)
u.EditorAssetLibrary.set_metadata_tag(mat, 'VIR.Source', 'Original VIR math-only water material; no imported bitmap')
u.EditorAssetLibrary.save_loaded_asset(mat)
