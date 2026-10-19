# Port sources (docs/PLAN.md §2.1). Each workstream adds its own subdirectory here.
add_subdirectory(platform)   # W2: SDL2 host layer (window, input, audio, main loop)
add_subdirectory(engine)     # W2: renderer, tick scheduler, input mapping (pure C)
