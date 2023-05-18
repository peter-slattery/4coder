
CUSTOM_UI_COMMAND_SIG(gs_lister_editable_search)
{
  Scratch_Block scratch(app);
  u8* space = push_array(scratch, u8, KB(1));
  String_Const_u8 needle = get_query_string(app, "Search: ", space, KB(1));
  if (needle.size == 0) return;
  
  View_ID target_view = get_next_view_after_active(app, Access_Always);
  Buffer_ID search_buffer = create_or_switch_to_buffer_and_clear_by_name(app, search_name, target_view);
  
  // list_all_locations__generic
  String_Const_u8_Array array = {&needle, 1};
  
  String_Match_Flag must_have_flags = 0;
  String_Match_Flag must_not_have_flags = 0;
  String_Match_List matches = find_all_matches_all_buffers(app, scratch, array, must_have_flags, must_not_have_flags);
  string_match_list_filter_remove_buffer(&matches, search_buffer);
  string_match_list_filter_remove_buffer_predicate(app, &matches, buffer_has_name_with_star);
  gs_print_string_match_list_to_buffer(app, search_buffer, matches);
  
}

global String_Const_u8 code_index_note_strs[] = {
  S8Lit("Type"),
  S8Lit("Function"),
  S8Lit("Macro"),
  S8Lit("None"),
};

function bool
note_is_of_kind(Code_Index_Note_Kind* kinds, i32 kinds_count, Code_Index_Note* note)
{
  bool result = false;
  for (i32 i = 0; i < kinds_count; i++)
  {
    if (kinds[i] == note->note_kind)
    {
      result = true;
      break;
    }
  }
  return result;
}

function void
lister_add_from_buffer_code_index_filtered(Lister* lister, Buffer_ID buffer, Arena* scratch, Code_Index_Note_Kind* kinds, i32 kinds_count, bool filter_all_but_last)
{
  Code_Index_File* file_notes = code_index_get_file(buffer);
  if (!file_notes) return;
  
  for (Code_Index_Note* note = file_notes->note_list.first;
       note != 0;
       note = note->next)
  {
    if (!note_is_of_kind(kinds, kinds_count, note)) continue;
    if (filter_all_but_last && note->next_in_hash) continue;
    
    String_Const_u8 sort = code_index_note_strs[note->note_kind];
    
    Tiny_Jump *jump = push_array(scratch, Tiny_Jump, 1);
    jump->buffer = buffer;
    jump->pos = note->pos.start;
    
    lister_add_item(lister, note->text, sort, jump, 0);
  }
}

function void
run_jump_lister(Application_Links* app, Lister* lister)
{
  Lister_Result l_result = run_lister(app, lister);
  Tiny_Jump result = {};
  if (!l_result.canceled && l_result.user_data != 0){
    block_copy_struct(&result, (Tiny_Jump*)l_result.user_data);
  }
  
  if (result.buffer != 0)
  {
    View_ID view = get_this_ctx_view(app, Access_Always);
    point_stack_push_view_cursor(app, view);
    jump_to_location(app, view, result.buffer, result.pos);
  }
}

function void
gs_lister_search_filtered(Application_Links* app, char* query, Code_Index_Note_Kind* allowed, i32 allowed_count, bool filter_all_but_last)
{
  Scratch_Block scratch(app);
  Lister_Block lister(app, scratch);
  lister_set_query(lister, query);
  lister_set_default_handlers(lister);
  
  for (Buffer_ID buffer = get_buffer_next(app, 0, Access_Always);
       buffer != 0; buffer = get_buffer_next(app, buffer, Access_Always))
  {
    lister_add_from_buffer_code_index_filtered(lister, buffer, scratch, allowed, allowed_count, filter_all_but_last);
  }
  run_jump_lister(app, lister);
}

CUSTOM_UI_COMMAND_SIG(gs_lister_search_types)
CUSTOM_DOC("Runs a search lister only on the types of the project")
{
  char *query = "Types:";
  Code_Index_Note_Kind allowed[] = {CodeIndexNote_Type};
  gs_lister_search_filtered(app, query, allowed, 1, true);
}

CUSTOM_UI_COMMAND_SIG(gs_lister_search_functions)
CUSTOM_DOC("Runs a search lister only on the functions of the project")
{
  char *query = "Functions:";
  Code_Index_Note_Kind allowed[] = {CodeIndexNote_Function};
  gs_lister_search_filtered(app, query, allowed, 1, true);
}

CUSTOM_UI_COMMAND_SIG(gs_lister_search_macros)
CUSTOM_DOC("Runs a search lister only on the macros of the project")
{
  char *query = "Macros:";
  Code_Index_Note_Kind allowed[] = {CodeIndexNote_Macro};
  gs_lister_search_filtered(app, query, allowed, 1, true);
}

CUSTOM_UI_COMMAND_SIG(gs_lister_search_all)
CUSTOM_DOC("Runs a search lister only on all of the project")
{
  char *query = "Search:";
  Code_Index_Note_Kind allowed[] = {
    CodeIndexNote_Macro,
    CodeIndexNote_Function,
    CodeIndexNote_Type,
  };
  gs_lister_search_filtered(app, query, allowed, 3, false);
}

//////////////////////////////////////
// Collection Lister

struct GS_Collection_Data
{
  String_Match_List matches;
  i32 count;
};

global String_Const_u8 collection_buffer_name = S8Lit("*collection*");
CUSTOM_ID(collection, collection_data_id);

CUSTOM_COMMAND_SIG(gs_search_collection)
CUSTOM_DOC("collects buffer pointers to the results of a search into one editable buffer")
{
  Scratch_Block scratch(app);
  
  View_ID target_view = get_next_view_after_active(app, Access_Always);
  Buffer_ID search_buffer = create_or_switch_to_buffer_and_clear_by_name(app, collection_buffer_name, target_view);
  
  Managed_Scope scope = buffer_get_managed_scope(app, search_buffer);
  GS_Collection_Data* collection_data = scope_attachment(app, scope, collection_data_id, GS_Collection_Data);
  
  collection_data->count += 1;
#if 0
  List_All_Locations_Flag flags = 0;
  u8 *space = push_array(scratch, u8, KB(1));
  String_Const_u8 needle = get_query_string(app, "List Locations For: ", space, KB(1));
  
  if (needle.size > 0){
    
    String_Match_Flag must_have_flags = 0;
    String_Match_Flag must_not_have_flags = 0;
    if (HasFlag(flags, ListAllLocationsFlag_CaseSensitive)){
      AddFlag(must_have_flags, StringMatch_CaseSensitive);
    }
    if (!HasFlag(flags, ListAllLocationsFlag_MatchSubstring)){
      AddFlag(must_not_have_flags, StringMatch_LeftSideSloppy);
      AddFlag(must_not_have_flags, StringMatch_RightSideSloppy);
    }
    
    String_Match_List matches = find_all_matches_all_buffers(app, scratch, needle, must_have_flags, must_not_have_flags);
    string_match_list_filter_remove_buffer(&matches, search_buffer);
    string_match_list_filter_remove_buffer_predicate(app, &matches, buffer_has_name_with_star);
  }
#endif
}