# Publish the Desktop Companion to GitHub

Suggested repository name: `esp32-desktop-companion`

Suggested description: `ESP32-S3 touchscreen desk companion with Sam voice interaction, YouTube Music controls, alarms, and printable screen and speaker covers.`

## Review before the first commit

1. Copy any remaining firmware or STL updates into the release folder and update the setup guide if the process changes.
2. Use the [setup guide](SETUP.md) for the on-device checks and test-fit both printed covers.
3. Review the [third-party code notices](THIRD_PARTY.md) and decide whether to add a license for your own work. Keep generated keys, SD credentials, recordings, backups, and local build outputs out of the commit.

## Create and push the repository

1. Sign in to GitHub and create a new **empty repository**. Choose its visibility. Private is useful for the first review while the project is still being edited. Leave GitHub's **Add README**, **Add .gitignore**, and **Choose a license** options unset for this first push.
2. Open PowerShell in the `GitHub-Release` folder, the one containing the project README and both STLs in `CAD`. Run the following and review the staged file list.

   ```powershell
   git init -b main
   git add .
   git status --short
   git diff --cached --stat
   ```

3. Commit and push. Replace `YOUR-ACCOUNT` and the repository name with the HTTPS URL GitHub shows for your new repository. Complete the GitHub sign-in prompt if Git asks for it.

   ```powershell
   git commit -m "Add ESP32-S3 Desktop Companion project"
   git remote add origin https://github.com/YOUR-ACCOUNT/esp32-desktop-companion.git
   git push -u origin main
   ```

4. Open the GitHub repository and check that the README renders, the setup guide opens, and both STL files can be downloaded from `CAD`. Clone it into a fresh folder and check that `Desktop_Companion.ino` opens from the `Desktop_Companion` folder.

[GitHub's Windows guide to pushing a local project](https://docs.github.com/en/migrations/importing-source-code/using-the-command-line-to-import-source-code/adding-locally-hosted-code-to-github?platform=windows)
