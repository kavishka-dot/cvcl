# Third-party headers

CVCL's PNG/JPEG support requires two single-file headers from the stb library.
They are not bundled (to keep the repo lean) -- download them once:

## Download

```bash
# Linux / macOS
curl -L https://raw.githubusercontent.com/nothings/stb/master/stb_image.h       -o stb_image.h
curl -L https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h -o stb_image_write.h
```

```powershell
# Windows PowerShell
Invoke-WebRequest https://raw.githubusercontent.com/nothings/stb/master/stb_image.h       -OutFile stb_image.h
Invoke-WebRequest https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h -OutFile stb_image_write.h
```

Then re-run CMake with `-DCVCL_WITH_STB=ON` (it is ON by default).

## License
stb headers are public domain / MIT. See https://github.com/nothings/stb
