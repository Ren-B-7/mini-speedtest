# Mini Project Template

This repository serves as a template for creating new "mini-projects" in the series. It provides a standardized structure and common configurations to help you get started quickly.

## Project Structure

-   **`src/`**: Contains your main source code files (`.c`).
    -   **`include/`**: Contains your header files (`.h`).
-   **`bin/`**: The compiled executable will be placed here.
-   **`Makefile`**: Build system configuration.
-   **`.clang-format`**: Code formatting configuration.
-   **`.clang-tidy`**: Static analysis configuration.
-   **`.gitignore`**: Specifies intentionally untracked files that Git should ignore.
-   **`LICENSE`**: The project's license.
-   **`README.md`**: This file.

## Getting Started

1.  **Clone this repository:**
    ```bash
    git clone <repository_url> my-new-project
    cd my-new-project
    ```
    Replace `<repository_url>` with the URL of this template repository.

2.  **Clean Git History (Optional but Recommended):**
    If you want a fresh Git history for your new project, you can do the following:
    ```bash
    rm -rf .git
    git init
    git add .
    git commit -m "Initial commit from template"
    # Add your remote origin if you plan to push to GitHub or another remote
    # git remote add origin <your_project_remote_url>
    ```

3.  **Update Project-Specific Files:**
    *   **`Makefile`**:
        *   Edit the `TARGET_NAME` variable to your project's name.
        *   Update the `SRCS` list to include all your `.c` files, ensuring they are prefixed with `src/`.
        *   Review and adjust `CFLAGS` and `LDFLAGS` as needed.
    *   **`README.md`**: Update this file to describe your specific project, its features, and any unique build instructions.
    *   **Source Files**: Replace `src/main.c` and `src/include/example.h` with your project's actual source code. Add any other `.c` files to the `src/` directory and corresponding `.h` files to `src/include/`.

4.  **Build and Run:**
    Use the provided `Makefile` commands:
    ```bash
    make all  # Build your project (executable will be in bin/)
    make run  # Run your project
    make clean # Clean build artifacts
    ```

## Configuration Files

-   **`.clang-format` & `.clang-tidy`**: These files are pre-configured for code style and basic linting. You can customize them to fit your preferences.
-   **`Makefile`**: A robust Makefile is provided. For projects with more complex build requirements (e.g., using libraries, custom build tools), you may need to modify this Makefile.

## License

This template is licensed under the MIT License.
