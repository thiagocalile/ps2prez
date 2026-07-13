from pathlib import Path

output_path = Path(r'./output')

image_paths = [image for image in output_path.iterdir() if 'png' in image.name]

with open('slides.h', 'w') as slide_header:

    slide_header.write("""
    typedef struct {
      unsigned char* payload;
      int size;
    } Slide;
    \n""")
    
    for image_number, image in enumerate(image_paths):
        slide_header.write(f"alignas(64) const unsigned char slide_{image_number} [] = ")
        slide_header.write("{\n")
        slide_header.write(f'  #embed "{str(image)}"\n')
        slide_header.write("};\n\n")

    slide_header.write("Slide presentation[] = {\n")
    for image_number, _ in enumerate(image_paths):
        slide_header.write("  {\n")
        slide_header.write(f"    .payload = slide_{image_number}, \n")
        slide_header.write(f"    .size = sizeof(slide_{image_number}),\n")
        slide_header.write("  },\n")
    slide_header.write("};")
