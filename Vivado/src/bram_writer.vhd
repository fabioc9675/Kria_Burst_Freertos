
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;
use IEEE.STD_LOGIC_UNSIGNED.ALL;

entity bram_writer is
    generic (
           MAX_SAMPLES : integer := 8192;
           ADDR_SIZE : integer := 14
    );
    Port ( clk : in STD_LOGIC;
           reset :  in std_logic;
           enb   : out std_logic;
           address : out std_logic_vector(ADDR_SIZE-1 downto 0);
           dout    : out std_logic_vector(31 downto 0);
           weo     : out std_logic);
end bram_writer;

architecture Behavioral of bram_writer is
signal contador : integer;
signal address_int : std_logic_vector(ADDR_SIZE-1 downto 0);

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
            
   contador <= (contador + 1) mod MAX_SAMPLES;
   address_int <= std_logic_vector(to_unsigned(contador * 4, ADDR_SIZE));
   dout <= std_logic_vector(to_unsigned(contador, 32));
end if;
end process;   


end Behavioral;


