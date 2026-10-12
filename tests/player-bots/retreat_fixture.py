"""Extract retreat production code for the standalone state/AI regression harness."""
def body(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

def generate(root, destination):
    zone = root / 'zone'
    bot = (zone / 'player_bot.cpp').read_text()
    config = (zone / 'player_bot_configuration.inc').read_text()
    ai = (zone / 'mob_ai.cpp').read_text()
    # These checks bind the harness to entry points that are compiled in the full server.
    assert 'return c && !Retreating() && !forced_sit_' in bot
    assert 'if(Retreating()||forced_sit_||' in (zone / 'player_bot_features.inc').read_text()
    assert 'if(PlayerBotRetreating(this)) return false;' in (zone / 'spells.cpp').read_text()
    assert 'if(Retreating()) BeginRetreat();' in config
    assert 'if(action=="retreat" && (sep->argnum!=3' in bot
    assert 'b->Retreating() && (action=="attack"||action=="assist"' in bot
    member_code = '\n'.join(body(bot, marker) for marker in (
        'bool Retreating() const', 'void BeginRetreat()'))
    destination.joinpath('retreat_members.inc').write_text(member_code)
    definitions = body(config, 'bool PlayerBot::SetBehavior(')
    definitions += '\n' + body(bot, 'bool PlayerBotRetreating(Mob* mob)')
    definitions += '\nvoid Mob::AI_Process() {\n' + body(ai, 'if(PlayerBotRetreating(this))')
    definitions += '\n++normal_ai_calls;\n}\n'
    destination.joinpath('retreat_definitions.inc').write_text(definitions)
