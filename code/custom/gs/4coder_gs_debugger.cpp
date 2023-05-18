
// Debugger Function Signatures
// 
// Any new debugger implementation needs to implement each of
// these prototypes, and reference them in its gs_debugger_spec
//
typedef String_Const_u8 gs_dbg_get_session_file(Application_Links* app, Arena* arena, String8 prj_root);
typedef void gs_dbg_begin_session(Application_Links* app, Arena* arena, String8 root, String_Const_u8 session_file);
typedef void gs_dbg_basic_cmd(Application_Links* app);

// Any new debugger implementation needs to create a global
// gs_debugger_spec variable, fill out its member function
// pointers.
//
// To use a particular debugger, set gs_debugger to point
// to that debuggers global gs_debugger_spec variable.
//   ie. gs_debugger = &gs_remedybg_spec;
//
struct gs_debugger_spec
{
  gs_dbg_get_session_file* get_session_file;
  gs_dbg_begin_session* begin_session;
  gs_dbg_basic_cmd* breakpoint_add;
  gs_dbg_basic_cmd* breakpoint_rem;
  gs_dbg_basic_cmd* run_to_cursor;
  gs_dbg_basic_cmd* jump_to_cursor;
  gs_dbg_basic_cmd* start_exe;
  gs_dbg_basic_cmd* stop_exe;
};

////////////////////////////////////////////////////////
// Debugger Backend: Remedy BG

function String_Const_u8
gs_debug_remedybg_get_session_file(Application_Links* app, Arena* arena, String8 prj_root)
{
  String_Const_u8 result = push_u8_stringf(
                                           arena, "%.*s/debug.rdbg", string_expand(prj_root)
                                           );
  
  // NOTE(PS): if the file doesn't exist, return an empty string
  if (!file_exists_and_is_file(app, result)) result.size = 0;
  return result;
}

function void
gs_debug_remedybg_begin_session(Application_Links* app, Arena* arena, String8 root, String_Const_u8 session_file)
{
  String_Const_u8 cmd = push_u8_stringf(
                                        arena, "remedybg %.*s", string_expand(session_file)
                                        );
  if (cmd.size == 0) return;
  
  exec_system_command(app, 0, buffer_identifier(0), root, cmd, 0);
}

// Source: https://gitlab.com/flyingsolomon/4coder_modal/-/blob/master/debugger_remedybg.cpp
function void
gs_debug_remedybg_command(Application_Links* app, String_Const_u8 command)
{
  Scratch_Block scratch(app);
  String_Const_u8 path = push_hot_directory(app, scratch);
  if(path.size > 0) 
  {
    String_Const_u8 cmd = push_u8_stringf(scratch, "remedybg %.*s", string_expand(command));
    if (cmd.size == 0) return;
    exec_system_command(app, 0, buffer_identifier(0), path, cmd, 0);
  }
}

// Source: https://gitlab.com/flyingsolomon/4coder_modal/-/blob/master/debugger_remedybg.cpp
function void 
gs_debug_remedybg_command_under_cursor(Application_Links *app, String_Const_u8 command) 
{
  Scratch_Block scratch(app);
  View_ID view = get_active_view(app, Access_Always);
  Buffer_ID buffer = view_get_buffer(app, view, Access_Read);
  
  String_Const_u8 file_name = push_buffer_file_name(app, scratch, buffer);
  String_Const_u8 path = push_hot_directory(app, scratch);
  
  i64 pos = view_get_cursor_pos(app, view);
  i64 line_number = get_line_number_from_pos(app, buffer, pos);
  
  if(file_name.size > 0 && path.size > 0) 
  {
    String_Const_u8 cmd = push_u8_stringf(
                                          scratch, "remedybg %.*s %.*s %lld", 
                                          string_expand(command), string_expand(file_name), line_number
                                          );
    
    if (cmd.size == 0) return;
    exec_system_command(app, 0, buffer_identifier(0), path, cmd, 0);
  }
}

function void
gs_debug_remedybg_breakpoint_add(Application_Links* app)
{
  gs_debug_remedybg_command_under_cursor(app, S8Lit("add-breakpoint-at-file"));
}

function void
gs_debug_remedybg_breakpoint_rem(Application_Links* app)
{
  gs_debug_remedybg_command_under_cursor(app, S8Lit("remove-breakpoint-at-file"));
}

function void
gs_debug_remedybg_run_to_cursor(Application_Links* app)
{
  gs_debug_remedybg_command_under_cursor(app, S8Lit("run-to-cursor"));
}

function void
gs_debug_remedybg_jump_to_cursor(Application_Links* app)
{
  gs_debug_remedybg_command_under_cursor(app, S8Lit("open-file"));
}

function void
gs_debug_remedybg_start_exe(Application_Links* app)
{
  gs_debug_remedybg_command(app, S8Lit("start-debugging"));
}

function void
gs_debug_remedybg_stop_exe(Application_Links* app)
{
  gs_debug_remedybg_command(app, S8Lit("stop-debugging"));
}

global gs_debugger_spec gs_remedybg_spec = {
  gs_debug_remedybg_get_session_file,
  gs_debug_remedybg_begin_session,
  gs_debug_remedybg_breakpoint_add,
  gs_debug_remedybg_breakpoint_rem,
  gs_debug_remedybg_run_to_cursor,
  gs_debug_remedybg_jump_to_cursor,
  gs_debug_remedybg_start_exe,
  gs_debug_remedybg_stop_exe
};

////////////////////////////////////////////////////////
// Commands

global gs_debugger_spec* gs_debugger = &gs_remedybg_spec;

CUSTOM_COMMAND_SIG(gs_debug_begin_session)
{
  if (!gs_debugger || !gs_debugger->get_session_file || !gs_debugger->begin_session) return;
  
  Scratch_Block scratch(app);
  
  Variable_Handle prj_var = vars_read_key(
                                          vars_get_root(), vars_save_string_lit("prj_config")
                                          );
  // TODO(PS): see what this is if there isn't a project loaded
  
  String8 root = prj_path_from_project(scratch, prj_var);
  if (root.size == 0) return;
  
  String_Const_u8 session_file = gs_debugger->get_session_file(app, scratch, root);
  if (session_file.size == 0) return;
  
  gs_debugger->begin_session(app, scratch, root, session_file);
}

CUSTOM_COMMAND_SIG(gs_debug_breakpoint_add)
{
  if (!gs_debugger || !gs_debugger->breakpoint_add) return;
  gs_debugger->breakpoint_add(app);
}

CUSTOM_COMMAND_SIG(gs_debug_breakpoint_rem)
{
  if (!gs_debugger || !gs_debugger->breakpoint_rem) return;
  gs_debugger->breakpoint_rem(app);
}

CUSTOM_COMMAND_SIG(gs_debug_run_to_cursor)
{
  if (!gs_debugger || !gs_debugger->run_to_cursor) return;
  gs_debugger->run_to_cursor(app);
}

CUSTOM_COMMAND_SIG(gs_debug_jump_to_cursor)
{
  if (!gs_debugger || !gs_debugger->jump_to_cursor) return;
  gs_debugger->jump_to_cursor(app);
}

CUSTOM_COMMAND_SIG(gs_debug_start_exe)
{
  if (!gs_debugger || !gs_debugger->start_exe) return;
  gs_debugger->start_exe(app);
}

CUSTOM_COMMAND_SIG(gs_debug_stop_exe)
{
  if (!gs_debugger || !gs_debugger->stop_exe) return;
  gs_debugger->stop_exe(app);
}