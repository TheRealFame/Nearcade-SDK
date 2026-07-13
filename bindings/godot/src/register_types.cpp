#include "register_types.h"
#include "nearcade_godot.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_nearcade_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    ClassDB::register_class<NearcadeSDK>();
    memnew(NearcadeSDK);
}

void uninitialize_nearcade_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    if (NearcadeSDK::get_singleton()) {
        memdelete(NearcadeSDK::get_singleton());
    }
}

extern "C" {

GDExtensionBool GDE_EXPORT nearcade_godot_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                                                 GDExtensionClassLibraryPtr p_library,
                                                 GDExtensionInitialization *r_initialization) {
    GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
    init_obj.register_initializer(initialize_nearcade_module);
    init_obj.register_terminator(uninitialize_nearcade_module);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}

}
