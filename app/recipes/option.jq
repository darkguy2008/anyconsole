def pick($values): $values[.] // $values[split("_")[0] // ""];
def option($key; $values): pick($values) as $chosen | if $chosen then {($key): $chosen} else {} end;
