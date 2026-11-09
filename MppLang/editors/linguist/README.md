# Getting "M++" recognized on GitHub

Until Linguist knows M++, `.gitattributes` shows `.mpp` files as Swift.

Linguist adds a language only when it is used widely: at least 2,000 `.mpp`
files indexed by GitHub search in the last year, excluding forks, spread over
many different users (the language's author is filtered out). Check with a
code search for `path:*.mpp NOT is:fork`.

When that's met:

1. Put the grammar in its own public repository with an open-source license
   (copy `../vscode/syntaxes/mpp.tmLanguage.json`; its scope is `source.mpp`).
2. Fork github-linguist/linguist and run `script/add-grammar <that repo URL>`.
3. Add `languages.yml` (this folder) to `lib/linguist/languages.yml`, then run
   `script/update-ids`.
4. Copy `samples/*.mpp` to `samples/M++/`.
5. Linguist already maps `.mpp` to nothing else, so no heuristic is needed.
6. Run the tests (`bundle exec rake test`) and open the PR, with the search link.

Then delete the `*.mpp linguist-language=Swift` line from `.gitattributes`.
