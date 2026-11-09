# M++ for Visual Studio Code

Swift-style syntax coloring for `.mpp` files. SwiftUI types and modifiers,
the Swift standard library and the M++ libraries are colored as system
symbols, and M++'s own keywords (`place`, `unplace`, `serial`, `parallel`)
as keywords. Brackets, comments and indentation work like Swift.

Install it by linking this folder into VS Code's extensions folder, then
reload VS Code:

    ln -s "$PWD" ~/.vscode/extensions/mpp-language

The same grammar works in any editor that reads TextMate grammars
(Sublime Text, Nova, TextMate, and GitHub's Linguist).
