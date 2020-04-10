-module(b64fast_tests).

-include_lib("eunit/include/eunit.hrl").

back_to_back_encode_test() ->
    Inputs = [
        42,
        foo,
        "foo",
        {foo},
        << "zany" >>,
        << "zan"  >>,
        << "za"   >>,
        << "z"    >>,
        <<        >>,
        binary:copy(<<"0123456789">>, 100000)
    ],
    lists:foreach(
        fun(Input) ->
            io:format("Running input: ~p...~n", [Input]),
            assert_encode(Input)
        end,
        Inputs
    ).

back_to_back_decode_test() ->
    Inputs = [
        42,
        foo,
        "foo",
        {foo},
        << "." >>,
        << "+" >>,
        << "!" >>,
        << "/" >>,
        << "Σ">>,
        << "a" >>,
        << "aa" >>,
        << "aaa" >>,
        << "aaaa" >>,
        << "aaaaa" >>,
        << "aaaaaa" >>,
        << "aaaaaaa" >>,
        << "aaaaaaaa" >>,
        << "aaaaaaaaa" >>,
        << "aaaaaaaaaa" >>,
        << "!~[]" >>,
        << "emFueQ==" >>,
        << "emFu"     >>,
        << "emE="     >>,
        << "eg=="     >>,
        <<            >>,
        << " emFu" >>,
        << "em Fu" >>,
        << "emFu " >>,
        << "    "  >>,
        << "   ="  >>,
        << "  =="  >>,
        << "=   "  >>,
        << "==  "  >>,
        << "\temFu">>,
        << "\tem  F  u">>,
        << "em  F  \t u">>,
        << "em  F  \tu">>,
        << "e\nm\nF\nu\n" >>,
        << "e\nm\nF\nu" >>,
        << "e\nm\nF\nu " >>,
        << "AAAA" >>,
        << "AAA=" >>,
        << "AAAA=" >>,
        << "AAA"  >>,
        << "AA==" >>,
        << "AA="  >>,
        << "AA"   >>,
        << "A=="  >>,
        << "A="   >>,
        << "A"    >>,
        << "=="   >>,
        << "="    >>,
        << "=a"   >>,
        << "==a"  >>,
        << "===a" >>,
        << "====a" >>,
        << "=====a" >>,
        << "=======a" >>,
        << "========a" >>,
        <<        >>,
        <<"PDw/Pz8+Pg==">>,
        <<"PDw:Pz8.Pg==">>,
        binary:copy(<<"a">>, 1000000),
        binary:copy(<<"a">>, 1000001),
        binary:copy(<<"a">>, 1000002),
        binary:copy(<<"a">>, 1000003),
        binary:copy(<<"a">>, 1000004),
        binary:copy(<<"a">>, 1000005),
        binary:copy(<<"a">>, 1000006),
        binary:copy(<<"a">>, 1000007),
        binary:copy(<<"a">>, 1000008),
        binary:copy(<<"a">>, 1000009),
        binary:copy(<<"0123456789">>, 100000),
        binary:copy(<<"0123456789_-">>, 100000)
    ],
    lists:foreach(
        fun(Input) ->
            io:format("Running input: ~p...~n", [Input]),
            assert_decode(Input)
        end,
        Inputs
    ).

assert_encode(Input) ->
    case catch encode(Input) of
        {'EXIT', {badarg, _}} ->
            ?assertException(error, badarg, b64fast:encode(Input));
        {'EXIT', {function_clause, _}} ->
            ?assertException(error, badarg, b64fast:encode(Input));
        {'EXIT', {badarith, _}} ->
            ?assertException(error, badarg, b64fast:encode(Input));
        Output ->
            ?assertEqual(Output, b64fast:encode(Input))
    end.

assert_decode(Input) ->
    case catch decode(Input) of
        {'EXIT', {badarg, _}} ->
            ?assertException(error, badarg, b64fast:decode(Input));
        {'EXIT', {function_clause, _}} ->
            ?assertException(error, badarg, b64fast:decode(Input));
        {'EXIT', {badarith, _}} ->
            ?assertException(error, badarg, b64fast:decode(Input));
        Output ->
            ?assertEqual(Output, b64fast:decode(Input))
    end.

speed_test() ->
    Data = binary:copy(<<"0123456789">>, 100000), % 1 MiB of data

    {Elapsed1, Enc1} = timer:tc(base64, encode, [Data]),
    io:fwrite(standard_error, "erlang encode ~B us ~f MiB/s~n",
      [Elapsed1, byte_size(Data) / Elapsed1]),

    {Elapsed2, _Dec1} = timer:tc(base64, decode, [Enc1]),
    io:fwrite(standard_error, "erlang decode ~B us ~f MiB/s~n",
      [Elapsed2, byte_size(Enc1) / Elapsed2]),

    {Elapsed3, Enc2} = timer:tc(b64fast, encode, [Data]),
    io:fwrite(standard_error, "NIF encode ~B us ~f MiB/s~n",
      [Elapsed3, byte_size(Data) / Elapsed3]),

    {Elapsed4, _Dec2} = timer:tc(b64fast, decode, [Enc2]),
    io:fwrite(standard_error, "NIF decode ~B us ~f MiB/s~n",
      [Elapsed4, byte_size(Enc2) / Elapsed4]).

speed10_test() ->
    Data = binary:copy(<<"0123456789">>, 1000000), % 10 MiB of data

    {Elapsed3, Enc2} = timer:tc(b64fast, encode, [Data]),
    io:fwrite(standard_error, "NIF encode ~B us ~f MiB/s~n",
      [Elapsed3, byte_size(Data) / Elapsed3]),

    {Elapsed4, _Dec2} = timer:tc(b64fast, decode, [Enc2]),
    io:fwrite(standard_error, "NIF decode ~B us ~f MiB/s~n",
      [Elapsed4, byte_size(Enc2) / Elapsed4]).

speed100_test() ->
    Data = binary:copy(<<"0123456789">>, 10000000), % 100 MiB of data

    {Elapsed3, Enc2} = timer:tc(b64fast, encode, [Data]),
    io:fwrite(standard_error, "NIF encode ~B us ~f MiB/s~n",
      [Elapsed3, byte_size(Data) / Elapsed3]),

    {Elapsed4, _Dec2} = timer:tc(b64fast, decode, [Enc2]),
    io:fwrite(standard_error, "NIF decode ~B us ~f MiB/s~n",
      [Elapsed4, byte_size(Enc2) / Elapsed4]).

encode(Bin) when is_binary(Bin) ->
    << << (urlencode_digit(D)) >> || <<D>> <= base64:encode(Bin), D =/= $= >>;
encode(L) when is_list(L) ->
    encode(iolist_to_binary(L));
encode(_) ->
    error(badarg).

decode(Bin) when is_binary(Bin) ->
    Bin2 = case byte_size(Bin) rem 4 of
        2 -> << Bin/binary, "==" >>;
        3 -> << Bin/binary, "=" >>;
        _ -> Bin
    end,
    base64:decode(<< << (urldecode_digit(D)) >> || <<D>> <= Bin2 >>);
decode(L) when is_list(L) ->
    decode(iolist_to_binary(L));
decode(_) ->
    error(badarg).

urlencode_digit($/) -> $_;
urlencode_digit($+) -> $-;
urlencode_digit(D)  -> D.

urldecode_digit($_) -> $/;
urldecode_digit($-) -> $+;
urldecode_digit(D)  -> D.
