import mcp.types as types

def get_prompts() -> list[types.Prompt]:
    return [
        types.Prompt(
            name="create-idle-loop",
            description="Template for creating a basic idle breathing loop for a character",
            arguments=[
                types.PromptArgument(
                    name="bone",
                    description="The main torso or spine bone to animate",
                    required=True
                )
            ]
        )
    ]

def get_prompt_message(name: str, arguments: dict) -> list[types.PromptMessage]:
    if name == "create-idle-loop":
        bone = arguments.get("bone", "spine")
        return [
            types.PromptMessage(
                role="user",
                content=types.TextContent(
                    type="text",
                    text=f"Please create a simple idle loop for the bone '{bone}'. "
                         f"At t=0 set rotation to 0, at t=1.0 set rotation to 5 degrees, "
                         f"and at t=2.0 set rotation back to 0. Make sure to save the project when done."
                )
            )
        ]
    raise ValueError(f"Unknown prompt: {name}")
