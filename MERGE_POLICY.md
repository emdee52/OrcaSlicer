# Merge policy

A feature branch is **not merged until the user has tested it**.

When a feature is code-complete:

1. Build it and run the tests.
2. Commit the verified milestone and push the feature branch.
3. Report it to the user, and stop there.

Merge `--no-ff` into the integration branch only after the user confirms the feature works in the
app. Tests passing, reviews clean and the build being green are not approval.
