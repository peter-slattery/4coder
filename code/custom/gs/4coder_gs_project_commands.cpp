
CUSTOM_COMMAND_SIG(gs_load_project)
CUSTOM_DOC("Looks for a project.4coder file in the current directory and tries to load it.  Looks in parent directories until a project file is found or there are no more parents.")
{
  // TODO(allen): compress this _thoughtfully_
  
  ProfileScope(app, "load project");
  save_all_dirty_buffers(app);
  Scratch_Block scratch(app);
  
  // NOTE(allen): Load the project file from the hot directory
  String8 project_path = push_hot_directory(app, scratch);
  File_Name_Data dump = dump_file_search_up_path(app, scratch, project_path, string_u8_litexpr("project.4coder"));
  String8 project_root = string_remove_last_folder(dump.file_name);
  
  if (dump.data.str == 0){
    print_message(app, string_u8_litexpr("Did not find project.4coder.\n"));
  }
  
  // NOTE(allen): Parse config data out of project file
  Config *config_parse = 0;
  Variable_Handle prj_var = vars_get_nil();
  if (dump.data.str != 0){
    Token_Array array = token_array_from_text(app, scratch, dump.data);
    if (array.tokens != 0){
      config_parse = def_config_parse(app, scratch, dump.file_name, dump.data, array);
      if (config_parse != 0){
        i32 version = 0;
        if (config_parse->version != 0){
          version = *config_parse->version;
        }
        
        switch (version){
          case 0:
          case 1:
          {
            prj_var = prj_v1_to_v2(app, project_root, config_parse);
          }break;
          default:
          {
            prj_var = def_fill_var_from_config(app, vars_get_root(), vars_save_string_lit("prj_config"), config_parse);
          }break;
        }
        
      }
    }
  }
  
  // NOTE(allen): Print Project
  if (!vars_is_nil(prj_var)){
    vars_print(app, prj_var);
    print_message(app, string_u8_litexpr("\n"));
  }
  
  // NOTE(allen): Print Errors
  if (config_parse != 0){
    String8 error_text = config_stringize_errors(app, scratch, config_parse);
    if (error_text.size > 0){
      print_message(app, string_u8_litexpr("Project errors:\n"));
      print_message(app, error_text);
      print_message(app, string_u8_litexpr("\n"));
    }
  }
  
  // NOTE(PS): Custom variable handling
  Variable_Handle virtual_whitespace_var = vars_read_key(prj_var, vars_save_string_lit("enable_virtual_whitespace"));
  if (virtual_whitespace_var.ptr != 0)
  {
    def_set_config_b32(
                       vars_save_string_lit("enable_virtual_whitespace"), 
                       vars_b32_from_var(virtual_whitespace_var)
                       );
  }
  
  // NOTE(allen): Open All Project Files
  Variable_Handle load_paths_var = vars_read_key(prj_var, vars_save_string_lit("load_paths"));
  Variable_Handle load_paths_os_var = vars_read_key(load_paths_var, vars_save_string_lit(OS_NAME));
  
  String_ID path_id = vars_save_string_lit("path");
  String_ID recursive_id = vars_save_string_lit("recursive");
  String_ID relative_id = vars_save_string_lit("relative");
  
  Variable_Handle whitelist_var = vars_read_key(prj_var, vars_save_string_lit("patterns"));
  Variable_Handle blacklist_var = vars_read_key(prj_var, vars_save_string_lit("blacklist_patterns"));
  
  Prj_Pattern_List whitelist = prj_pattern_list_from_var(scratch, whitelist_var);
  Prj_Pattern_List blacklist = prj_pattern_list_from_var(scratch, blacklist_var);
  
  for (Variable_Handle load_path_var = vars_first_child(load_paths_os_var);
       !vars_is_nil(load_path_var);
       load_path_var = vars_next_sibling(load_path_var)){
    Variable_Handle path_var = vars_read_key(load_path_var, path_id);
    Variable_Handle recursive_var = vars_read_key(load_path_var, recursive_id);
    Variable_Handle relative_var = vars_read_key(load_path_var, relative_id);
    
    String8 path = vars_string_from_var(scratch, path_var);
    b32 recursive = vars_b32_from_var(recursive_var);
    b32 relative = vars_b32_from_var(relative_var);
    
    
    u32 flags = 0;
    if (recursive){
      flags |= PrjOpenFileFlag_Recursive;
    }
    
    String8 file_dir = path;
    if (relative){
      String8 prj_dir = prj_path_from_project(scratch, prj_var);
      
      String8List file_dir_list = {};
      string_list_push(scratch, &file_dir_list, prj_dir);
      string_list_push_overlap(scratch, &file_dir_list, '/', path);
      string_list_push_overlap(scratch, &file_dir_list, '/', SCu8());
      file_dir = string_list_flatten(scratch, file_dir_list, StringFill_NullTerminate);
    }
    
    prj_open_files_pattern_filter(app, file_dir, whitelist, blacklist, flags);
  }
  
  // NOTE(allen): Set Window Title
  Variable_Handle proj_name_var = vars_read_key(prj_var, vars_save_string_lit("project_name"));
  String_ID proj_name_id = vars_string_id_from_var(proj_name_var);
  if (proj_name_id != 0){
    String8 proj_name = vars_read_string(scratch, proj_name_id);
    String8 title = push_u8_stringf(scratch, "4coder project: %.*s", string_expand(proj_name));
    set_window_title(app, title);
  }
}

CUSTOM_COMMAND_SIG(reopen_all)
CUSTOM_DOC("Reopen all buffers from the hard drive.")
{
  for (Buffer_ID buffer = get_buffer_next(app, 0, Access_Always);
       buffer != 0;
       buffer = get_buffer_next(app, buffer, Access_Always))
  {
    if (!buffer_has_name_with_star(app, buffer)){
      buffer_reopen(app, buffer, 0);
    }
  }
}

