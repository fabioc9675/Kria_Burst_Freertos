
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;
use IEEE.STD_LOGIC_UNSIGNED.ALL;

entity memory_bram is
    Port ( clk : in STD_LOGIC;
           reset :  in std_logic;
           enb   : out std_logic;
           address : out std_logic_vector(15 downto 0);
           dout    : out std_logic_vector(31 downto 0);
           weo     : out std_logic);
end memory_bram;

architecture Behavioral of memory_bram is
signal contador : integer;
signal address_int : std_logic_vector(15 downto 0);

begin

enb <= '1';
weo <= '1';
address <= address_int;

process(clk,reset)
begin

if reset = '0' then
   contador <= 0;
   address_int <= (others => '0');
elsif rising_edge(clk) then
            
   contador <= (contador + 1) mod 48000;
   address_int <= std_logic_vector(to_unsigned(contador, 16));
   dout <= std_logic_vector(to_unsigned(contador, 32));
end if;
end process;   


end Behavioral;


