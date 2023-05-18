
#if !defined(FCODER_GS_CUSTOM_CPP)
#define FCODER_GS_CUSTOM_CPP

#include "4coder_default_include.cpp"

#define S8Lit(s) string_u8_litexpr(s)

global View_ID global_compilation_view;
global b32     global_compilation_view_expanded = 0;

function void
gs_setup_essential_mapping(Mapping *mapping, i64 global_id, i64 file_id, i64 code_id){
  MappingScope();
  SelectMapping(mapping);
  
  SelectMap(global_id);
  BindCore(gs_startup, CoreCode_Startup);
  BindCore(default_try_exit, CoreCode_TryExit);
  BindCore(clipboard_record_clip, CoreCode_NewClipboardContents);
  BindMouseWheel(mouse_wheel_scroll);
  BindMouseWheel(mouse_wheel_change_face_size, KeyCode_Control);
  
  SelectMap(file_id);
  ParentMap(global_id);
  BindTextInput(write_text_input);
  BindMouse(click_set_cursor_and_mark, MouseCode_Left);
  BindMouseRelease(click_set_cursor, MouseCode_Left);
  BindCore(click_set_cursor_and_mark, CoreCode_ClickActivateView);
  BindMouseMove(click_set_cursor_if_lbutton);
  
  SelectMap(code_id);
  ParentMap(file_id);
  BindTextInput(gs_write_text_and_auto_indent);
}

// NOTE(allen): Users can declare their own managed IDs here.
global u32 gs_modal_mode_input = 0;
global u32 gs_modal_mode_cmd   = 1;
global u32 gs_modal_mode_debug = 2;

#include "4coder_gs_modal.cpp"
#include "4coder_gs_search.cpp"
#include "4coder_gs_listers.cpp"
#include "4coder_gs_collections.cpp"
#include "4coder_gs_draw.cpp"
#include "4coder_gs_handlers.cpp"
#include "4coder_gs_debugger.cpp"
#include "4coder_gs_project_commands.cpp"

// Must come last
#include "4coder_gs_custom_bindings.cpp"

#if !defined(META_PASS)
#include "generated/managed_id_metadata.cpp"
#endif

void
custom_layer_init(Application_Links *app){
  Thread_Context *tctx = get_thread_context(app);
  
  // NOTE(allen): setup for default framework
  default_framework_init(app);
  
  // NOTE(allen): default hooks and command maps
  set_all_gs_hooks(app);
  
  gs_collection_init(1024, 1024, tctx);
  
  gs_modal_init(3, tctx);
  gs_bindings();
}

#endif // FCODER_GS_CUSTOM_CPP
