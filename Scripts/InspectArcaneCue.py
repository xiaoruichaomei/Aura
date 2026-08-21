import unreal

cue = unreal.EditorAssetLibrary.load_asset('/Game/Blueprints/AbilitySystem/Cues/GC_ArcaneShards')
unreal.log_warning('ARCANE_CUE asset={}'.format(cue))
if cue:
    generated_class = cue.generated_class()
    default_object = unreal.get_default_object(generated_class)
    unreal.log_warning('ARCANE_CUE generated_class={}'.format(generated_class))
    for prop in ['gameplay_cue_tag', 'default_spawn_condition', 'default_placement_info', 'burst_effects']:
        try:
            value = default_object.get_editor_property(prop)
            unreal.log_warning('ARCANE_CUE {}={}'.format(prop, value))
            if prop == 'gameplay_cue_tag':
                unreal.log_warning('ARCANE_CUE tag_name={}'.format(value.get_editor_property('tag_name')))
        except Exception as error:
            unreal.log_warning('ARCANE_CUE {} unavailable={}'.format(prop, error))

    for structure_name in ['default_spawn_condition', 'default_placement_info', 'burst_effects']:
        try:
            structure = default_object.get_editor_property(structure_name)
            unreal.log_warning('ARCANE_CUE {} export={}'.format(structure_name, structure.export_text()))
        except Exception as error:
            unreal.log_warning('ARCANE_CUE {} export unavailable={}'.format(structure_name, error))
