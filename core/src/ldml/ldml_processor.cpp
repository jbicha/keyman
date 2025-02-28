/*
  Copyright:    © SIL International.
  Description:  This is an implementation of the LDML keyboard spec 3.0.
  Create Date:  5 Aug 2022
  Authors:      Marc Durdin (MD)
*/

#include <fstream>
#include <algorithm>
#include "ldml/ldml_processor.hpp"
#include "ldml/ldml_transforms.hpp"
#include "ldml/ldml_markers.hpp"
#include "state.hpp"
#include "kmx_file.h"
#include "kmx/kmx_plus.h"
#include "kmx/kmx_xstring.h"
#include "kmx/kmx_processevent.h"
#include "ldml/keyman_core_ldml.h"
#include "kmx/kmx_file_validator.hpp"
#include "debuglog.h"
#include <assert.h>

namespace {
  constexpr km_core_attr const engine_attrs = {
    256,
    KM_CORE_LIB_CURRENT,
    KM_CORE_LIB_AGE,
    KM_CORE_LIB_REVISION,
    KM_CORE_TECH_LDML,
    "SIL International"
  };
}

// using km::core::kmx::ShouldDebug; // for DebugLog

namespace km {
namespace core {


ldml_processor::ldml_processor(path const & kb_path, const std::vector<uint8_t> &data)
: abstract_processor(
    keyboard_attributes(kb_path.stem(), KM_CORE_LMDL_PROCESSOR_VERSION, kb_path.parent(), {})
  ), _valid(false), transforms(), bksp_transforms(), keys()
{

  if(data.size() <= sizeof(kmx::COMP_KEYBOARD_EX)) {
    DebugLog("data.size %zu too small", data.size());
    return;
  }

//   // Locate the structs here, but still retain ptrs to the raw structs.
  kmx::KMX_FileValidator* comp_keyboard = (kmx::KMX_FileValidator*)data.data();

  // Perform the standard validation
  if(!comp_keyboard->VerifyKeyboard(data.size())) {
    DebugLog("VerifyKeyboard() returned false");
    return;
  }

  kmx::kmx_plus kplus(comp_keyboard, data.size()); // slices to a COMP_KEYBOARD

  if(!kplus.is_valid()) {
    DebugLog("kmx_plus.is_valid is false");
    return;
  }

  // Now, if we have keys, use them.
  if (kplus.key2 != nullptr) {
    // read all keys into array
    for (KMX_DWORD i=0; i<kplus.key2->kmapCount; i++) {
      std::u16string str;
      auto kmapEntry = kplus.key2Helper.getKmap(i);
      assert(kmapEntry != nullptr);
      // now look up the key
      auto keyEntry = kplus.key2Helper.getKeys(kmapEntry->key);
      assert(keyEntry != nullptr);

      if (keyEntry->flags & LDML_KEYS_KEY_FLAGS_EXTEND) {
        if (nullptr == kplus.strs) {
          DebugLog("for keys: kplus.strs == nullptr"); // need a string table to get strings
          assert(false);
          return;
        }
        str = kplus.strs->get(keyEntry->to);
      } else {
        str = keyEntry->get_to_string();
      }
      keys.add((km_core_virtual_key)kmapEntry->vkey, (uint16_t)kmapEntry->mod, str);
    }
  } // else: no keys! but still valid. Just, no keys.

  // load transforms
  if (kplus.tran != nullptr && kplus.tran->groupCount > 0) {
    transforms.reset(km::core::ldml::transforms::load(kplus, kplus.tran, kplus.tranHelper));
    if (!transforms) {
      DebugLog("Failed to load tran transforms");
      return; // failed to load
    }
  }

  // load bksp transforms
  if (kplus.bksp != nullptr && kplus.bksp->groupCount > 0) {
    bksp_transforms.reset(km::core::ldml::transforms::load(kplus, kplus.bksp, kplus.bkspHelper));
    if (!bksp_transforms) {
      DebugLog("Failed to load bksp transforms");
      return; // failed to load
    }
  }
  // Only valid if we reach here
  DebugLog("_valid = true");
  _valid = true;
}

bool ldml_processor::is_kmxplus_file(path const & kb_path, std::vector<uint8_t>& data) {
// TODO-LDML: we should refactor all the core components to delegate file loading
//            to the Engine, which requires an API change, but this makes delivery
//            of keyboard files more flexible under more WASM.

  std::ifstream file(static_cast<std::string>(kb_path), std::ios::binary | std::ios::ate);
  if(!file.good()) {
    return false;
  }
  const std::streamsize size = file.tellg();
  if(size >= KMX_MAX_ALLOWED_FILE_SIZE) {
    return false;
  }

  file.seekg(0, std::ios::beg);

  data.resize((size_t)size);
  if(!file.read((char *) data.data(), size)) {
    return false;
  }

  file.close();

  const kmx::PCOMP_KEYBOARD comp_keyboard = (kmx::PCOMP_KEYBOARD)data.data();

  if(comp_keyboard->dwIdentifier != KMX_DWORD(FILEID_COMPILED)) {
    return false;
  }

  if(comp_keyboard->dwFileVersion < VERSION_160 || (comp_keyboard->dwFlags & KF_KMXPLUS) == 0) {
    return false;
  }

  // A KMXPlus file is in the buffer (although more validation is required and will
  // be done in the constructor)
  return true;
}

km_core_status
ldml_processor::process_queued_actions(
  km_core_state *state
) {
  assert(state);
  if (!state)
    return KM_CORE_STATUS_INVALID_ARGUMENT;
  // TODO Implement
  return KM_CORE_STATUS_OK;
}

bool ldml_processor::queue_action(
  km_core_state * state,
  km_core_action_item const* action_item
)
{
  assert(state);
  assert(action_item);
  if ((!state) || (!action_item))
    return false;
  return false;
}

km_core_status
ldml_processor::process_event(
    km_core_state *state,
    km_core_virtual_key vk,
    uint16_t modifier_state,
    uint8_t is_key_down,
    uint16_t _kmn_unused(event_flags)
) {
  assert(state);
  if (!state)
    return KM_CORE_STATUS_INVALID_ARGUMENT;

  try {
    // At the start of every process_event always clear the action_items
    state->actions().clear();

    if (!is_key_down) {
      process_key_up(state, vk, modifier_state);
    } else {
      switch (vk) {
      // Currently, only one VK gets spoecial treatment.
      // Special handling for backspace VK
      case KM_CORE_VKEY_BKSP:
        process_backspace(state);
        break;
      default:
        // all other VKs
        process_key_down(state, vk, modifier_state);
      } // end of switch
    } // end of normal processing

    // all key-up and key-down events end up here.
    state->actions().commit();  // always commit
    return KM_CORE_STATUS_OK;
  } catch (std::bad_alloc &) {
    // out of memory, clean up and get out
    state->actions().clear();
    return KM_CORE_STATUS_NO_MEM;
  }
}

void
ldml_processor::process_key_up(km_core_state *state, km_core_virtual_key _kmn_unused(vk), uint16_t _kmn_unused(modifier_state))
    const {
  // TODO-LDML: Implement caps lock handling
  state->actions().clear();  // TODO-LDML: Why is clear here?
}

void
ldml_processor::process_backspace(km_core_state *state) const {
  if (!!bksp_transforms) {
    // process with an empty string
    auto matchedContext = process_output(state, std::u32string(), bksp_transforms.get());

    if (matchedContext > 0) {
      return; // The transform took care of the backspacing.
    }  // else, fall through to default processing below.
  }

  // Find out what the last actual character was and remove it.
  // attempt to get the last char
  // TODO-LDML: emoji backspace
  auto end = state->context().rbegin();
  if (end != state->context().rend()) {
    if ((*end).type == KM_CORE_CT_CHAR) {
      state->actions().push_backspace(KM_CORE_BT_CHAR, (*end).character);
      state->context().pop_back();
      return;
    } else if ((*end).type == KM_CORE_BT_MARKER) {
      state->actions().push_backspace(KM_CORE_BT_MARKER, (*end).marker);
      state->context().pop_back();
    }
  }
  /*
    We couldn't find a character at end of context (context is empty),
    so we'll pass the backspace keystroke on to the app to process; the
    app might want to use backspace to move between contexts or delete
    a text box, etc. Or it might be a legacy app and we've had our caret
    dumped in somewhere unknown, so we will have to depend on the app to
    be sensible about backspacing because we know nothing.
  */
  state->actions().push_backspace(KM_CORE_BT_UNKNOWN);
}

void
ldml_processor::process_key_down(km_core_state *state, km_core_virtual_key vk, uint16_t modifier_state) const {
  // Look up the key
  bool found = false;
  const std::u16string key_str = keys.lookup(vk, modifier_state, found);

  if (!found) {
    // no key was found, so pass the keystroke on to the Engine
    emit_invalidate_passthrough_keystroke(state, vk, modifier_state);
  } else if (!key_str.empty()) {
    process_key_string(state, key_str);
  } // else no action: It's a gap or gap-like key.
}

void
ldml_processor::process_key_string(km_core_state *state, const std::u16string &key_str) const {
  // We know that key_str is not empty per the caller.
  assert(!key_str.empty());

  // we convert the keys str to UTF-32 here instead of using the emit_text() overload
  // so that we don't have to reconvert it inside the transform code.
  std::u32string key_str32 = kmx::u16string_to_u32string(key_str);
  (void)process_output(state, key_str32, transforms.get());
}

size_t ldml_processor::process_output(km_core_state *state, const std::u32string &str, ldml::transforms *with_transforms) const {
  std::u32string nfd_str = str;
  // Note:
  //  The normalize functions have assert and Debuglog at the bottom.
  //  so we do not need to assert the status here unless we're going to do something
  //  different with control flow.
  (void)ldml::normalize_nfd_markers(nfd_str);

  // extract context string, in NFD
  std::u32string old_ctxtstr_nfd;
  (void)context_to_string(state, old_ctxtstr_nfd, true);
  (void)ldml::normalize_nfd_markers(old_ctxtstr_nfd);

  // context string in NFD
  std::u32string ctxtstr;
  (void)context_to_string(state, ctxtstr, true); // with markers
  // add the newly added key output to ctxtstr
  ctxtstr.append(nfd_str);
  (void)ldml::normalize_nfd_markers(ctxtstr);
  /** transform output string */
  std::u32string outputString;
  /** how many chars of the ctxtstr to replace */
  size_t matchedContext = 0; // zero if no transforms

  // begin modifications to the string

  if(with_transforms != nullptr) {
    matchedContext = with_transforms->apply(ctxtstr, outputString);
  } // else: no transforms, no output

  // Short Circuit: if no transforms matched, and no new text is being output,
  // just return.
  if (matchedContext == 0 && str.empty()) {
    return matchedContext;
  }

  // drop last 'matchedContext':
  ctxtstr.resize(ctxtstr.length() - matchedContext);
  ctxtstr.append(outputString); // TODO-LDML: should be able to do a normalization-safe append here.
  (void)ldml::normalize_nfd_markers(ctxtstr);

  // Ok. We've done all the happy manipulations.

  /** NFD w/ markers */
  std::u32string ctxtstr_cleanedup = ctxtstr;
  (void)ldml::normalize_nfd_markers(ctxtstr_cleanedup);

  // find common prefix.
  // For example, if the context previously had "aaBBBBB" and it is changing to "aaCCC" then we will have:
  // - old_ctxtstr_changed = "BBBBB"
  // - new_ctxtstr_changed = "CCC"
  // So the BBBBB needs to be removed and then CCC added.
  auto ctxt_prefix = mismatch(old_ctxtstr_nfd.begin(), old_ctxtstr_nfd.end(), ctxtstr_cleanedup.begin(), ctxtstr_cleanedup.end());

  // handle a special case where we're simply changing from one marker to another.
  // Example:
  // 0. old_ctxtstr_changed ends with  … U+FFFF U+0008 | U+0001 …
  // 1. ctxtstr_cleanedup   ends with  … U+FFFF U+0008 | U+0002 …
  // Pipe symbol shows where the difference starts.
  // As you can see, the different starts in the MIDDLE of a marker sequence.
  // so, old_ctxtstr_changed will start with U+0001
  // and new_ctxtstr_changed will start with U+0002
  // remove_text will only be able to delete up to and through the U+0001
  // and it will need to emit a push_backspace(KM_CORE_BT_MARKER,…) due to the
  // marker change.
  // We can detect this because the unchanged_prefix will end with u+FFFF U+0008
  //
  // Oh, and yes, test case 'regex-test-8a-0' hits this.
  std::u32string common_prefix(old_ctxtstr_nfd.begin(), ctxt_prefix.first);
  if (common_prefix.length() >= 2) {
    auto iter = common_prefix.rbegin();
    if (*(iter++) == LDML_MARKER_CODE && *(iter++) == UC_SENTINEL) {
      // adjust the iterator so that the "U+FFFF U+0008" is not a part of the common prefix.
      ctxt_prefix.first -= 2;
      ctxt_prefix.second += 2;
      // Now, old_ctxtstr_changed and new_ctxtstr_changed will start with U+FFFF U+0008 …
    }
  }

  /** The part of the old string to be removed */
  std::u32string old_ctxtstr_changed(ctxt_prefix.first,old_ctxtstr_nfd.end());
  /** The new context to be added */
  std::u32string new_ctxtstr_changed(ctxt_prefix.second,ctxtstr_cleanedup.end());

  // FIRST drop the old suffix. Note: this mutates old_ctxtstr_changed.
  // see remove_text() docs, this PUSHes actions, POPs context items, and TRIMS the string.
  remove_text(state, old_ctxtstr_changed, old_ctxtstr_changed.length());
  assert(old_ctxtstr_changed.length() == 0);
  // old_ctxtstr_changed is now empty because it's been removed.
  // context is "aa" in the above example.

  // THEN add the new suffix, "CCC" in the above example
  emit_text(state, new_ctxtstr_changed);
  // context is now "aaCCC"

  return matchedContext;
}

void
ldml_processor::remove_text(km_core_state *state, std::u32string &str, size_t length) {
  // str is the string to remove, so it should be at least as long as length
  assert(length <= str.length());
  /** track how many context items have been removed, via push_backspace() */
  size_t contextRemoved = 0;
  for (auto c = state->context().rbegin(); length > 0 && c != state->context().rend(); c++, contextRemoved++) {
    /** last char of context */
    km_core_usv lastCtx = str.back();
    uint8_t type        = c->type;
    assert(type == KM_CORE_BT_CHAR || type == KM_CORE_BT_MARKER);
    if (type == KM_CORE_BT_CHAR) {
      // single char, drop it
      length--;
      assert(c->character == lastCtx);
      str.pop_back();
      // Cause prior char to be removed
      state->actions().push_backspace(KM_CORE_BT_CHAR, c->character);
    } else if (type == KM_CORE_BT_MARKER) {
      assert(length >= 3);
      state->actions().push_backspace(KM_CORE_BT_MARKER, c->marker);
      // #3 - the marker.
      assert(lastCtx == c->marker);
      str.pop_back();
      length--;
      // #2 - the code
      assert(str.back() == LDML_MARKER_CODE);
      str.pop_back();
      length--;
      // #1 - the sentinel
      assert(str.back() == UC_SENTINEL);
      str.pop_back();
      length--;
    }
  }
  assert(length == 0);
  // now, pop the context items
  for (size_t i = 0; i < contextRemoved; i++) {
    // we don't pop during the above loop because the iterator gets confused
    state->context().pop_back();
  }
}

km_core_attr const & ldml_processor::attributes() const {
  return engine_attrs;
}

km_core_keyboard_key  * ldml_processor::get_key_list() const {
  km_core_keyboard_key* key_list = new km_core_keyboard_key(KM_CORE_KEYBOARD_KEY_LIST_END);
  return key_list;
}

km_core_keyboard_imx  * ldml_processor::get_imx_list() const {
  km_core_keyboard_imx* imx_list = new km_core_keyboard_imx(KM_CORE_KEYBOARD_IMX_END);
  return imx_list;
}

km_core_context_item * ldml_processor::get_intermediate_context() {
  km_core_context_item *citems = new km_core_context_item(KM_CORE_CONTEXT_ITEM_END);
  return citems;
}

km_core_status ldml_processor::validate() const {
  return _valid ? KM_CORE_STATUS_OK : KM_CORE_STATUS_INVALID_KEYBOARD;
}

void
ldml_processor::emit_text(km_core_state *state, const std::u16string &str) {
  const std::u32string str32 = kmx::u16string_to_u32string(str);
  emit_text(state, str32);
}

void
ldml_processor::emit_text(km_core_state *state, const std::u32string &str) {
  for (auto it = str.begin(); it < str.end(); it++) {
    const auto ch = *it;
    // If we are at the start of a sequence:
    if (ch == LDML_UC_SENTINEL) {
      it++; // consume LDML_UC_SENTINEL
      // TODO-LDML: Might assert if a malformed sequence is included- "should not happen"?
      assert(it < str.end());
      // verify that the next char is LDML_MARKER_CODE
      assert(*it == LDML_MARKER_CODE);
      it++; // consume LDML_MARKER_CODE
      assert(it < str.end());
      const auto marker_no = *it;
      emit_marker(state, marker_no);
    } else {
      emit_text(state, ch);
    }
  }
}

void
ldml_processor::emit_text(km_core_state *state, km_core_usv ch) {
  assert(ch != LDML_UC_SENTINEL);
  state->context().push_character(ch);
  state->actions().push_character(ch);
}

void
ldml_processor::emit_marker(km_core_state *state, KMX_DWORD marker_no) {
  assert(km::core::kmx::is_valid_marker(marker_no));
  state->actions().push_marker(marker_no);
  state->context().push_marker(marker_no);
}

void ldml_processor::emit_invalidate_passthrough_keystroke(km_core_state *state, km_core_virtual_key vk, uint16_t _kmn_unused(modifier_state)) {
  if ((vk < 0x100) && km::core::kmx::vkey_to_contextreset[vk]) {
    state->actions().push_invalidate_context();
  } else {
    assert(vk < 0x100); // don't expect synthetic vkeys here
  }
  state->actions().push_emit_keystroke();
}

size_t
ldml_processor::context_to_string(km_core_state *state, std::u32string &str, bool include_markers) {
    str.clear();
    auto &cp      = state->context();
    size_t ctxlen = 0; // TODO-LDML: not used by callers?
    uint8_t last_type = KM_CORE_BT_UNKNOWN;
    for (auto c = cp.rbegin(); c != cp.rend(); c++, ctxlen++) {
      last_type = c->type;
      if (last_type == KM_CORE_BT_CHAR) {
        str.insert(0, 1, c->character);
      } else if (last_type == KM_CORE_BT_MARKER) {
        assert(km::core::kmx::is_valid_marker(c->marker));
        if (include_markers) {
          ldml::prepend_marker(str, c->marker);
        }
      } else {
        break;
      }
    }
    return ctxlen; // consumed the entire context buffer.
}

} // namespace core
} // namespace km
