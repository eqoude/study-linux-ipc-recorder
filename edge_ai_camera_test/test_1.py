from transformers import pipeline

pipe = pipeline("image-text-to-text", model="HuggingFaceTB/SmolVLM2-500M-Video-Instruct")
messages = [
    {
        "role": "user",
        "content": [
            {"type": "image", "url": "https://huggingface.co/datasets/huggingface/documentation-images/resolve/main/p-blog/candy.JPG"},
            {"type": "text", "text": "What animal is on the candy?"}
        ]
    },
]
result = pipe(text=messages)
print(result)




'''
pipe = pipeline(
    "image-text-to-text",
    model="HuggingFaceTB/SmolVLM2-500M-Video-Instruct"
)

    第一次运行时会去 Hugging Face 下载模型文件，例如：

        config.json
        model.safetensors
        tokenizer.json
        processor_config.json
        preprocessor_config.json
        generation_config.json

    下载完成后，默认会缓存到你用户目录下：

        ~/.cache/huggingface/hub/
'''