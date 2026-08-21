import unreal

asset_path = '/Game/Blueprints/AbilitySystem/Cues/GC_ArcaneShards'
cue = unreal.EditorAssetLibrary.load_asset(asset_path)
if not cue:
    raise RuntimeError('Unable to load {}'.format(asset_path))

default_object = unreal.get_default_object(cue.generated_class())
cue_tag = unreal.GameplayTag()
cue_tag.set_editor_property('tag_name', 'GameplayCue.ArcaneShards')
default_object.set_editor_property('gameplay_cue_tag', cue_tag)

unreal.KismetEditorUtilities.compile_blueprint(cue)
unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)

saved_tag = default_object.get_editor_property('gameplay_cue_tag')
unreal.log_warning('ARCANE_CUE_FIXED tag={}'.format(saved_tag))
