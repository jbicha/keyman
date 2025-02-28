#include "pch.h"
#include "kmprocessactions.cpp"

// Test the Process Actions private functions
// TODO: The following actions are not tested KM_CORE_IT_ALERT, KM_CORE_IT_PERSIST_OPT, KM_CORE_IT_EMIT_KEYSTROKE

// Fixture for kmprocessactons tests
class KMPROCESSACTIONS : public ::testing::Test {
public:
  KMPROCESSACTIONS() {}

  void
  SetUp() {
    Globals_InitProcess();
  }

  void
  TearDown() {
    UninitialiseProcess(FALSE);
    Globals_UninitProcess();
  }

  ~KMPROCESSACTIONS() {}
};

// KM_CORE_IT_CHAR - processUnicodeChar
TEST_F(KMPROCESSACTIONS, processUnicodeChartest) {

  WCHAR callbuf[MAXCONTEXT];
  AITIP testApp;
  WCHAR *expectedContext = L"A";
  km_core_action_item itemAddChar = { KM_CORE_IT_CHAR, {0,}, {'A'}};

  processUnicodeChar(&testApp, &itemAddChar);
  WCHAR *contextBuf = testApp.ContextBufMax(MAXCONTEXT);
  EXPECT_STREQ(contextBuf, expectedContext);

  km_core_usv testSurrogateChar    = Uni_SurrogateToUTF32(0xD801, 0xDC37);  //𐐷';
  km_core_action_item itemAddChar2 = {KM_CORE_IT_CHAR, {0,}, {testSurrogateChar}};
  WCHAR expectedStringSurrogate[] = {'A', 0xD801, 0xDC37, 0};
  processUnicodeChar(&testApp, &itemAddChar2);
  contextBuf = testApp.ContextBufMax(MAXCONTEXT);
  EXPECT_STREQ(contextBuf, &expectedStringSurrogate[0]);
}

// KM_CORE_IT_MARKER - processMarker Deadkey
TEST_F(KMPROCESSACTIONS, processMarkertest) {

  WCHAR callbuf[MAXCONTEXT];
  AITIP testApp;
  WCHAR expectedContext[] = {UC_SENTINEL, CODE_DEADKEY, 2, 0};
  uint32_t marker               = 2;
  km_core_action_item itemAddMarker = {KM_CORE_IT_MARKER, {0,}, {marker}};

  processMarker(&testApp, &itemAddMarker);
  WCHAR *contextBuf = testApp.ContextBufMax(MAXCONTEXT);
  EXPECT_STREQ(contextBuf, expectedContext);
}

// KM_CORE_IT_BACK - processBack
// First test processing a backspace for a deadkey
TEST_F(KMPROCESSACTIONS, processBackDeadkeytest) {

  WCHAR callbuf[MAXCONTEXT];
  AITIP testApp;
  WCHAR expectedContext[] = {'A', 0};
  km_core_action_item itemAddChar = {KM_CORE_IT_CHAR, {0,}, {'A'}};

  processUnicodeChar(&testApp, &itemAddChar);
  uint32_t marker                 = 2;
  km_core_action_item itemAddMarker = {KM_CORE_IT_MARKER, {0,}, {marker}};
  processMarker(&testApp, &itemAddMarker);
  km_core_action_item itemBackSpace = {KM_CORE_IT_BACK};
  itemBackSpace.backspace.expected_type  = KM_CORE_IT_MARKER;
  itemBackSpace.backspace.expected_value = marker;
  processBack(&testApp, &itemBackSpace);
  WCHAR *contextBuf = testApp.ContextBufMax(MAXCONTEXT);
  EXPECT_STREQ(contextBuf, expectedContext);
}

// KM_CORE_IT_BACK - processBack
// Press Backspace for a normal character
// Also test for Unknown Character
TEST_F(KMPROCESSACTIONS, processBackCharactertest) {

  WCHAR callbuf[MAXCONTEXT];
  AITIP testApp;
  WCHAR expectedContext[] = {'A', 0};
  WCHAR expectedContextFinal[] = {0};
  km_core_action_item itemAddChar = {KM_CORE_IT_CHAR, {0,}, {'A'}};

  processUnicodeChar(&testApp, &itemAddChar);
  itemAddChar.character            = 'B';
  processUnicodeChar(&testApp, &itemAddChar);
  km_core_action_item itemBackSpace       = {KM_CORE_IT_BACK};
  itemBackSpace.backspace.expected_type  = KM_CORE_IT_CHAR;
  itemBackSpace.backspace.expected_value = 'B';
  // backspace
  processBack(&testApp, &itemBackSpace);
  WCHAR *contextBuf = testApp.ContextBufMax(MAXCONTEXT);
  EXPECT_STREQ(contextBuf, expectedContext);
  // backspace for unknown it should backspace a character
  itemBackSpace.type = KM_CORE_BT_UNKNOWN;
  processBack(&testApp, &itemBackSpace);
  contextBuf = testApp.ContextBufMax(MAXCONTEXT);
  EXPECT_STREQ(contextBuf, expectedContextFinal);
}


// KM_CORE_IT_BACK - processBack
// Press Backspace for a surrogate character.
// TODO: This test doesn't test for the PostKey handling
// of TSF and two backspaces being sent to the App.
TEST_F(KMPROCESSACTIONS, processBackSurrogateTSFtest) {
  WCHAR callbuf[MAXCONTEXT];
  AITIP testApp;
  WCHAR expectedContext[]         = { 0 };
  WCHAR expectedStringSurrogate[] = {0xD801, 0xDC37, 0};

  km_core_usv testSurrogateChar    = Uni_SurrogateToUTF32(0xD801, 0xDC37);  //𐐷';
  km_core_action_item itemAddChar  = {KM_CORE_IT_CHAR, {0,}, {testSurrogateChar}};
  processUnicodeChar(&testApp, &itemAddChar);

  km_core_action_item itemBackSpace       = {KM_CORE_IT_BACK};
  itemBackSpace.backspace.expected_type  = KM_CORE_IT_CHAR;
  itemBackSpace.backspace.expected_value = testSurrogateChar;
  WCHAR *contextBuf                      = testApp.ContextBufMax(MAXCONTEXT);
  EXPECT_STREQ(contextBuf, expectedStringSurrogate);
  // backspace
  processBack(&testApp, &itemBackSpace);
  contextBuf = testApp.ContextBufMax(MAXCONTEXT);
  EXPECT_STREQ(contextBuf, expectedContext);
}

// KM_CORE_IT_BACK - processBack
// Press Backspace for a character doesn't match expected character
// Note currently we don't check for a character match this should be updated

TEST_F(KMPROCESSACTIONS, processBackUnexpectedChartest) {

  WCHAR callbuf[MAXCONTEXT];
  AITIP testApp;
  WCHAR expectedContext[] = {'A', 0};
  km_core_action_item itemAddChar = {KM_CORE_IT_CHAR, {0,}, {'A'}};

  processUnicodeChar(&testApp, &itemAddChar);
  itemAddChar.character = 'C';
  processUnicodeChar(&testApp, &itemAddChar);
  km_core_action_item itemBackSpace       = {KM_CORE_IT_BACK};
  itemBackSpace.backspace.expected_type  = KM_CORE_IT_CHAR;
  itemBackSpace.backspace.expected_value = 'B';
  // backspace
  processBack(&testApp, &itemBackSpace);
  WCHAR *contextBuf = testApp.ContextBufMax(MAXCONTEXT);
  EXPECT_STREQ(contextBuf, expectedContext);
}

// KM_CORE_IT_INVALIDATE_CONTEXT - processInvalidateContext
TEST_F(KMPROCESSACTIONS, processInvalidateContextTest) {

  WCHAR callbuf[MAXCONTEXT];
  AITIP testApp;
  WCHAR expectedContext[] = {0};
  km_core_action_item itemAddChar = {KM_CORE_IT_CHAR, {0,}, {'A'}};

  processUnicodeChar(&testApp, &itemAddChar);
  itemAddChar.character = 'B';
  processUnicodeChar(&testApp, &itemAddChar);

  // A keyboard a state is need to test processInvalidateContext
  km_core_option_item test_env_opts[] = {{u"hello", u"world", KM_CORE_OPT_KEYBOARD}, KM_CORE_OPTIONS_END};
  km_core_keyboard *testKB = nullptr;
  km_core_state *testState = nullptr;
  km_core_path_name dummyPath      = L"dummyActions.mock";
  EXPECT_EQ(km_core_keyboard_load(dummyPath, &testKB), KM_CORE_STATUS_OK);
  EXPECT_EQ(km_core_state_create(testKB, test_env_opts, &testState), KM_CORE_STATUS_OK);

  processInvalidateContext(&testApp,testState);
  WCHAR *contextBuf = testApp.ContextBufMax(MAXCONTEXT);
  EXPECT_STREQ(contextBuf, expectedContext);

  // dispose keyboard
  km_core_state_dispose(testState);
  km_core_keyboard_dispose(testKB);

}
