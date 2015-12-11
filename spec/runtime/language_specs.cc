#include "runtime/runtime_spec_helper.h"
#include <functional>
#include <set>
#include <utility>
#include "runtime/length.h"
#include "runtime/helpers/read_test_entries.h"
#include "runtime/helpers/spy_input.h"
#include "runtime/helpers/log_debugger.h"
#include "runtime/helpers/point_helpers.h"

extern "C" const TSLanguage *ts_language_javascript();
extern "C" const TSLanguage *ts_language_json();
extern "C" const TSLanguage *ts_language_arithmetic();
extern "C" const TSLanguage *ts_language_golang();
extern "C" const TSLanguage *ts_language_c();
extern "C" const TSLanguage *ts_language_cpp();

map<string, const TSLanguage *> languages({
  {"json", ts_language_json()},
  {"arithmetic", ts_language_arithmetic()},
  {"javascript", ts_language_javascript()},
  {"golang", ts_language_golang()},
  {"c", ts_language_c()},
  {"cpp", ts_language_cpp()},
});

void expect_the_correct_tree(TSNode node, TSDocument *doc, string tree_string) {
  const char *node_string = ts_node_string(node, doc);
  AssertThat(node_string, Equals(tree_string));
  free((void *)node_string);
}

void expect_a_consistent_tree(TSNode node, TSDocument *doc) {
  size_t child_count = ts_node_child_count(node);
  size_t start_char = ts_node_start_char(node);
  size_t end_char = ts_node_end_char(node);
  TSPoint start_point = ts_node_start_point(node);
  TSPoint end_point = ts_node_end_point(node);
  bool has_changes = ts_node_has_changes(node);
  bool some_child_has_changes = false;

  AssertThat(start_char, !IsGreaterThan(end_char));
  AssertThat(start_point, !IsGreaterThan(end_point));

  size_t last_child_end_char = 0;
  TSPoint last_child_end_point = {0, 0};

  for (size_t i = 0; i < child_count; i++) {
    TSNode child = ts_node_child(node, i);
    size_t child_start_char = ts_node_start_char(child);
    size_t child_end_char = ts_node_end_char(child);
    TSPoint child_start_point = ts_node_start_point(child);
    TSPoint child_end_point = ts_node_end_point(child);

    if (i > 0) {
      AssertThat(child_start_char, !IsLessThan(last_child_end_char));
      AssertThat(child_start_point, !IsLessThan(last_child_end_point));
      last_child_end_char = child_end_char;
      last_child_end_point = child_end_point;
    }

    AssertThat(child_start_char, !IsLessThan(start_char));
    AssertThat(child_end_char, !IsGreaterThan(end_char));
    AssertThat(child_start_point, !IsLessThan(start_point));
    AssertThat(child_end_point, !IsGreaterThan(end_point));

    expect_a_consistent_tree(child, doc);

    if (ts_node_has_changes(child))
      some_child_has_changes = true;
  }

  if (child_count > 0)
    AssertThat(has_changes, Equals(some_child_has_changes));
}

string random_string(char min, char max) {
  string result;
  size_t length = random() % 12;
  for (size_t i = 0; i < length; i++) {
    char inserted_char = min + (random() % (max - min));
    result += inserted_char;
  }
  return result;
}

string random_char(string characters) {
  size_t index = random() % characters.size();
  return string() + characters[index];
}

string random_words(size_t count) {
  string result;
  bool just_inserted_word = false;
  for (size_t i = 0; i < count; i++) {
    if (random() % 10 < 6) {
      result += random_char("!(){}[]<>+-=");
    } else {
      if (just_inserted_word)
        result += " ";
      result += random_string('a', 'z');
      just_inserted_word = true;
    }
  }
  return result;
}

START_TEST

describe("Languages", [&]() {
  for (const auto &pair : languages) {
    describe(("The " + pair.first + " parser").c_str(), [&]() {
      TSDocument *doc;

      before_each([&]() {
        doc = ts_document_make();
        ts_document_set_language(doc, pair.second);
        // ts_document_set_debugger(doc, log_debugger_make(true));
      });

      after_each([&]() {
        ts_document_free(doc);
      });

      for (auto &entry : test_entries_for_language(pair.first)) {
        SpyInput *input;

        auto it_handles_edit_sequence = [&](string name, std::function<void()> edit_sequence){
          it(("parses " + entry.description + ": " + name).c_str(), [&]() {
            input = new SpyInput(entry.input, 3);
            ts_document_set_input(doc, input->input());
            edit_sequence();
            TSNode root_node = ts_document_root_node(doc);
            expect_the_correct_tree(root_node, doc, entry.tree_string);
            expect_a_consistent_tree(root_node, doc);
            delete input;
          });
        };

        it_handles_edit_sequence("initial parse", [&]() {
          ts_document_parse(doc);
        });

        std::set<std::pair<size_t, size_t>> deletions;
        std::set<std::pair<size_t, string>> insertions;

        for (size_t i = 0; i < 50; i++) {
          size_t edit_position = random() % entry.input.size();
          size_t deletion_size = random() % (entry.input.size() - edit_position);
          string inserted_text = random_words(random() % 4 + 1);

          if (insertions.insert({edit_position, inserted_text}).second) {
            string description = "\"" + inserted_text + "\" at " + to_string(edit_position);

            it_handles_edit_sequence("repairing an insertion of " + description, [&]() {
              ts_document_edit(doc, input->replace(edit_position, 0, inserted_text));
              ts_document_parse(doc);

              ts_document_edit(doc, input->undo());
              ts_document_parse(doc);
            });

            it_handles_edit_sequence("performing and repairing an insertion of " + description, [&]() {
              ts_document_parse(doc);

              ts_document_edit(doc, input->replace(edit_position, 0, inserted_text));
              ts_document_parse(doc);

              ts_document_edit(doc, input->undo());
              ts_document_parse(doc);
            });
          }

          if (deletions.insert({edit_position, deletion_size}).second) {
            string desription = to_string(edit_position) + "-" + to_string(edit_position + deletion_size);

            it_handles_edit_sequence("repairing a deletion of " + desription, [&]() {
              ts_document_edit(doc, input->replace(edit_position, deletion_size, ""));
              ts_document_parse(doc);

              ts_document_edit(doc, input->undo());
              ts_document_parse(doc);
            });

            it_handles_edit_sequence("performing and repairing a deletion of " + desription, [&]() {
              ts_document_parse(doc);

              ts_document_edit(doc, input->replace(edit_position, deletion_size, ""));
              ts_document_parse(doc);

              ts_document_edit(doc, input->undo());
              ts_document_parse(doc);
            });
          }
        }
      }
    });
  }
});

END_TEST