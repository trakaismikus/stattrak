"""Tests of the instructions for AI coding tools: AGENTS.md, the guides in skills/, and the start
text that a person pastes into an AI tool (README.md and the Get started page).

An AI tool follows these texts word for word. A guide that names a file or a command that is not
there sends the tool, and the person, the wrong way. These tests keep the texts and the project
in step.
"""

import io
import os
import re
import sys
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))

import app  # noqa: E402

GUIDES = ("stattrak-app", "stattrak-firmware", "stattrak-screens")
# The texts that an AI tool reads as instructions
TEXTS = ("AGENTS.md", "docs/APP_STORE.md", "skills/stattrak-app/SKILL.md", "skills/stattrak-screens/SKILL.md",
         "skills/stattrak-firmware/SKILL.md", "skills/stattrak-firmware/code.md")
MAX_AGENTS_LINES = 400      # AGENTS.md is in the memory of the AI tool for each task: keep it short


def read(path):
    with io.open(os.path.join(REPO_ROOT, path), encoding="utf-8") as handle:
        return handle.read()


def front_matter(text):
    """The name and the description of a SKILL.md file"""
    found = re.match(r"---\n(.*?)\n---\n", text, re.S)
    if not found:
        return None
    values = {}
    for line in found.group(1).splitlines():
        key, _, value = line.partition(":")
        values[key.strip()] = value.strip()
    return values


def sections(text):
    """The numbers of the headings of a text: {"1", "1.2", "4", ...}"""
    return set(re.findall(r"(?m)^#{2,3} (\d+(?:\.\d+)?)[. ]", text))


class Guides(unittest.TestCase):
    def test_each_guide_has_a_name_and_a_description(self):
        self.assertEqual(sorted(os.listdir(os.path.join(REPO_ROOT, "skills"))), sorted(GUIDES))
        for name in GUIDES:
            values = front_matter(read("skills/%s/SKILL.md" % name))
            self.assertIsNotNone(values, name)
            self.assertEqual(values.get("name"), name)        # the Agent Skills format: the name is the folder
            self.assertTrue(20 <= len(values.get("description", "")) <= 1024, name)
            self.assertIn("Use ", values["description"], name)   # it says when a tool must load the guide

    def test_the_pointer_files_name_the_same_guide(self):
        # .claude/skills and .agents/skills: where AI tools look for skills. Real files and no
        # links: git on Windows makes a text file from a link.
        for folder in (".claude/skills", ".agents/skills"):
            self.assertEqual(sorted(os.listdir(os.path.join(REPO_ROOT, folder))), sorted(GUIDES), folder)
            for name in GUIDES:
                path = os.path.join(REPO_ROOT, folder, name, "SKILL.md")
                self.assertFalse(os.path.islink(path), path)
                text = read("%s/%s/SKILL.md" % (folder, name))
                self.assertEqual(front_matter(text), front_matter(read("skills/%s/SKILL.md" % name)),
                                 "%s/%s: the name or the description is not the same as in skills/" % (folder, name))
                self.assertIn("`skills/%s/SKILL.md`" % name, text)

    def test_agents_md_is_short_and_names_each_guide(self):
        text = read("AGENTS.md")
        self.assertLessEqual(text.count("\n"), MAX_AGENTS_LINES,
                             "AGENTS.md got long. Put the text of one task into its guide in skills/.")
        for name in GUIDES:
            self.assertIn("`skills/%s/SKILL.md`" % name, text)
        self.assertIn("`skills/stattrak-firmware/code.md`", text)
        # The safety rules are in the file that each AI tool reads for each task
        self.assertIn("## 3. The most important rule: do not brick the device", text)
        self.assertIn("### Never do these things", text)
        # CLAUDE.md and GEMINI.md send their tools to AGENTS.md
        self.assertIn("@AGENTS.md", read("CLAUDE.md"))
        self.assertIn("AGENTS.md", read("GEMINI.md"))

    def test_each_file_that_a_text_names_is_there(self):
        for path in TEXTS:
            for name in set(re.findall(r"`((?:skills|docs|tools|tests|apps|designs|updater|sim)/[\w./-]+\.\w+)`", read(path))):
                if "<" in name or "NAME" in name or name.startswith("apps/dice/"):
                    continue
                self.assertTrue(os.path.exists(os.path.join(REPO_ROOT, name)), "%s names %s" % (path, name))

    def test_each_section_that_a_text_names_is_there(self):
        agents = sections(read("AGENTS.md"))
        targets = {
            r"`AGENTS\.md`,? section (\d+(?:\.\d+)?)": agents,
            r"[Tt]he app guide, section (\d+)": sections(read("skills/stattrak-app/SKILL.md")),
            r"[Tt]he firmware guide, section (\d+)": sections(read("skills/stattrak-firmware/SKILL.md")),
            r"[Tt]he code guide, section (\d+)": sections(read("skills/stattrak-firmware/code.md")),
            r"`docs/APP_STORE\.md`,? section (\d+)": sections(read("docs/APP_STORE.md")),
            r"`docs/APPS\.md`,? sections? (\d+(?:\.\d+)?)": sections(read("docs/APPS.md")),
        }
        for path in TEXTS + ("docs/APPS.md", "docs/DESIGNS.md", "CONTRIBUTING.md"):
            text = re.sub(r"\s+", " ", read(path))
            for pattern, there in targets.items():
                for number in re.findall(pattern, text):
                    self.assertIn(number, there, "%s: %s%s" % (path, pattern[:24], number))

        # A reference inside AGENTS.md (without the references to a guide or to an other document)
        own = re.sub(r"(`[^`]*`|guide),? \(?sections? \d+[^.|\n]{0,12}", "", read("AGENTS.md"))
        for number in re.findall(r"[Ss]ections? (\d+(?:\.\d+)?)", own):
            self.assertIn(number, agents, "AGENTS.md: section %s" % number)

    def test_each_app_command_that_a_text_names_is_there(self):
        commands = {"new", "build", "check", "describe", "picture", "run", "catalog", "sim", "store-check"}
        for path in TEXTS + ("README.md", "docs/APPS.md"):
            for command in re.findall(r"python tools/app\.py ([a-z-]+)", read(path)):
                self.assertIn(command, commands, "%s: python tools/app.py %s" % (path, command))
        # And the tool has each of them
        for command in sorted(commands):
            with self.assertRaises(SystemExit) as stop, io.StringIO() as out:
                old = sys.stdout
                sys.stdout = out
                try:
                    app.main([command, "--help"])
                finally:
                    sys.stdout = old
            self.assertEqual(stop.exception.code, 0, command)

    def test_the_fault_texts_of_the_app_guide_are_the_texts_of_the_tool(self):
        guide = read("skills/stattrak-app/SKILL.md")
        for result in ("bad index", "too many steps"):
            self.assertIn(result, app.RESULTS)
            self.assertIn(result, guide)
        with self.assertRaises(app.AppError) as error:
            app.compile_source('app "T"\non draw {\n  text(0, 0, "123456789012345678901", text, on, left)\n}\n')
        self.assertIn("has 21 characters; 20 is the most", str(error.exception))
        self.assertIn("has 21 characters; 20 is the most", guide)
        for path in ("tests/stattrak_sim.py", "skills/stattrak-app/SKILL.md", "skills/stattrak-firmware/SKILL.md"):
            self.assertTrue("The headless simulator is not built" in read(path), path)


class StartText(unittest.TestCase):
    """The text that a person pastes into an AI tool: the first thing in the README and on the
    Get started page."""

    def readme_text(self):
        return re.search(r"```text\n(.*?)```", read("README.md"), re.S).group(1)

    def page_text(self):
        found = re.search(r'<pre id="appPrompt">(.*?)</pre>', read("updater/start/index.html"), re.S).group(1)
        return found.replace("&quot;", '"').replace("&amp;", "&")

    def test_the_readme_and_the_page_have_the_same_text(self):
        def words(text):
            return re.sub(r"\s+", " ", text).strip()

        self.assertEqual(words(self.readme_text()), words(self.page_text()))

    def test_the_text_is_first_and_it_asks_for_a_picture(self):
        readme = read("README.md")
        self.assertLess(readme.index("```text"), readme.index("\n## ", readme.index("## ") + 3),
                        "The start text is in the first section of the README")
        text = self.readme_text()
        self.assertIn("git clone https://github.com/trakaismikus/stattrak.git", text)
        self.assertIn("read the file AGENTS.md completely", text)
        self.assertIn("show me a picture", text)
        self.assertIn("Ask me before you install anything", text)
        self.assertIn("My idea: ", text)                      # a ready example, not an empty line
        self.assertNotIn("section", text)                      # no section number that can change
        self.assertLessEqual(len(text.splitlines()), 14)

    def test_the_example_idea_is_an_app_that_the_tool_can_make(self):
        # The idea of the start text (a dice) must work with the fast start of AGENTS.md section 1.1
        agents = read("AGENTS.md")
        self.assertIn("### 1.1 The fast start", agents)
        self.assertIn("dice", self.readme_text())
        source = ('app "DICE"\nvar n = 1\non press2 { n = 1 + random(6) }\n'
                  'on draw {\n  bignum(55, 5, n, 1, 3, on)\n}\n')
        machine = app.Machine(app.compile_source(source)["package"], look=True)
        self.assertEqual(machine.run("open"), "ok")
        self.assertEqual(app.run_actions(machine, ["press2", "wait:100", "frame"])[-1].count("#"), 0)
        self.assertTrue(1 <= machine.globals[0] <= 6)
        self.assertEqual(machine.look_lines(), [])


if __name__ == "__main__":
    unittest.main()
