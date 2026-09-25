"""Run in Unreal Editor Python to create the cooked-in hull/oar water effect.

This material is deliberately separate from downloaded Han content. The course
actor keeps a small pool of non-colliding planes and sets Age/Opacity only for
visible, presentation-derived effects. Review the graph and chase view before
shipping; the recipe alone does not create a cooked asset.
"""
import unreal as u

TARGET = '/Game/Water/M_WaterInteraction'
L = u.MaterialEditingLibrary
MP = u.MaterialProperty
folder, name = TARGET.rsplit('/', 1)
mat = u.load_asset(TARGET) or u.AssetToolsHelpers.get_asset_tools().create_asset(
    name, folder, u.Material, u.MaterialFactoryNew())
# Rewire existing outputs rather than deleting inspected expressions. UE 5.8
# can assert while deleting an expression rooted by editor inspection.
mat.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property('shading_model', u.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property('two_sided', True)


def node(cls, x, y, **props):
    result = L.create_material_expression(mat, cls, x, y)
    for key, value in props.items():
        result.set_editor_property(key, value)
    return result


def const(value, x, y):
    return node(u.MaterialExpressionConstant, x, y, r=value)


def binary(cls, a, b, x, y):
    result = node(cls, x, y)
    L.connect_material_expressions(a, '', result, 'A')
    L.connect_material_expressions(b, '', result, 'B')
    return result


def mul(a, b, x, y):
    return binary(u.MaterialExpressionMultiply, a, b, x, y)


def add(a, b, x, y):
    return binary(u.MaterialExpressionAdd, a, b, x, y)


def unary(cls, source, x, y):
    result = node(cls, x, y)
    L.connect_material_expressions(source, '', result, '')
    return result


uv = node(u.MaterialExpressionTextureCoordinate, -1200, 0)
center = node(u.MaterialExpressionConstant2Vector, -1200, 120, r=.5, g=.5)
offset = binary(u.MaterialExpressionSubtract, uv, center, -1000, 0)
radius2 = binary(u.MaterialExpressionDotProduct, offset, offset, -800, 0)
radius = unary(u.MaterialExpressionSquareRoot, radius2, -600, 0)
age = node(u.MaterialExpressionScalarParameter, -800, 200, parameter_name='Age', default_value=0, group='Effect')
kind = node(u.MaterialExpressionScalarParameter, -800, 300, parameter_name='EffectKind', default_value=0, group='Effect')
opacity = node(u.MaterialExpressionScalarParameter, -800, 400, parameter_name='Opacity', default_value=0, group='Effect')

# A narrow expanding ring for a blade crossing; a broad, feathered patch for
# the hull trail. Both use the plane's UVs, so world-space pool positions stay
# fixed while only size and opacity change over their short lives.
ring_radius = add(const(.13, -600, 240), mul(age, const(.29, -600, 320), -400, 260), -200, 240)
ring_delta = unary(u.MaterialExpressionAbs, binary(u.MaterialExpressionSubtract, radius, ring_radius, 0, 200), 200, 200)
ring = unary(u.MaterialExpressionSaturate,
             add(const(1.0, 200, 280), mul(ring_delta, const(-28.0, 200, 360), 400, 280), 600, 240), 800, 240)
wake_base = unary(u.MaterialExpressionSaturate,
                  add(const(1.0, -400, 500), mul(radius, const(-2.2, -400, 580), -200, 500), 0, 500), 200, 500)
wake = mul(wake_base, wake_base, 400, 500)
shape = node(u.MaterialExpressionLinearInterpolate, 1000, 350)
L.connect_material_expressions(wake, '', shape, 'A')
L.connect_material_expressions(ring, '', shape, 'B')
L.connect_material_expressions(kind, '', shape, 'Alpha')
alpha = mul(shape, opacity, 1200, 350)
L.connect_material_property(alpha, '', MP.MP_OPACITY)
color = node(u.MaterialExpressionVectorParameter, 1000, 550, parameter_name='EffectTint',
             default_value=u.LinearColor(.20, .30, .37, 1), group='Effect')
L.connect_material_property(mul(color, alpha, 1400, 500), '', MP.MP_EMISSIVE_COLOR)

L.recompile_material(mat)
u.EditorAssetLibrary.set_metadata_tag(mat, 'VIR.Source', 'Original VIR cosmetic water interaction; cooked-in, not Han content')
u.EditorAssetLibrary.save_loaded_asset(mat)
