def can_build(env, platform):
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "BehaviorCallMethod",
        "BehaviorCooldown",
        "BehaviorInverter",
        "BehaviorParallel",
        "BehaviorRepeat",
        "BehaviorSelector",
        "BehaviorSequence",
        "BehaviorState",
        "BehaviorStateMachine",
        "BehaviorTask",
        "BehaviorTimeLimit",
        "BehaviorTree",
        "BehaviorTreePlayer",
        "BehaviorWait",
    ]


def get_doc_path():
    return "doc_classes"
