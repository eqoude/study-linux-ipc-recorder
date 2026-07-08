from transformers import pipeline

pipe = pipeline("image-text-to-text", model="./models/SmolVLM2-500M-Video-Instruct",)
messages = [
    {
        "role": "user",
        "content": [
            {"type": "image", "url": "./edge_ai_camera_test/candy.JPG"},
            {"type": "text", "text": "What animal is on the candy?"}
        ]
    },
]
result = pipe(text=messages)
print(result)


