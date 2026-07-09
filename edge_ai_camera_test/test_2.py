from transformers import pipeline

pipe = pipeline("image-text-to-text", model="./models/SmolVLM2-500M-Video-Instruct",)
messages = [
    {
        "role": "user",
        "content": [
            {"type": "image", "url": "./edge_ai_camera_test/candy.JPG"},
            {"type": "text", "text": "Can you describe this image?"}
        ]
    },
]
result = pipe(text=messages)
print(result)


