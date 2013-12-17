--
-- Copyright (C) 2009-2012 Chris McClelland
--
-- This program is free software: you can redistribute it and/or modify
-- it under the terms of the GNU Lesser General Public License as published by
-- the Free Software Foundation, either version 3 of the License, or
-- (at your option) any later version.
--
-- This program is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU Lesser General Public License for more details.
--
-- You should have received a copy of the GNU Lesser General Public License
-- along with this program.  If not, see <http://www.gnu.org/licenses/>.
--
library ieee;

use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity spi_talk is
	generic (
		NUM_DEVS     : integer := 1
	);
	port(
		clk_in       : in  std_logic;

		-- DVR interface -----------------------------------------------------------------------------
		chanAddr_in  : in  std_logic_vector(6 downto 0);  -- the selected channel (0-127)

		-- Host >> FPGA pipe:
		h2fData_in   : in  std_logic_vector(7 downto 0);  -- data lines used when the host writes to a channel
		h2fValid_in  : in  std_logic;                     -- '1' means "on the next clock rising edge, please accept the data on h2fData"
		h2fReady_out : out std_logic;                     -- channel logic can drive this low to say "I'm not ready for more data yet"

		-- Host << FPGA pipe:
		f2hData_out  : out std_logic_vector(7 downto 0);  -- data lines used when the host reads from a channel
		f2hValid_out : out std_logic;                     -- channel logic can drive this low to say "I don't have data ready for you"
		f2hReady_in  : in  std_logic;                     -- '1' means "on the next clock rising edge, put your next byte of data on f2hData"

		-- Peripheral interface ----------------------------------------------------------------------
		spiClk_out   : out   std_logic;
		spiData_out  : out   std_logic;
		spiData_in   : in    std_logic;
		spiCS_out    : out   std_logic_vector(NUM_DEVS-1 downto 0)
	);
end entity;

architecture rtl of spi_talk is
	signal sendData    : std_logic_vector(7 downto 0);
	signal sendValid   : std_logic;
	signal sendReady   : std_logic;

	signal recvData    : std_logic_vector(7 downto 0);
	signal recvValid   : std_logic;
	signal recvReady   : std_logic;

	signal fifoData    : std_logic_vector(7 downto 0);
	signal fifoValid   : std_logic;
	signal fifoReady   : std_logic;

	signal config      : std_logic_vector(NUM_DEVS+1 downto 0);
	signal config_next : std_logic_vector(NUM_DEVS+1 downto 0);
	constant TURBO     : integer := 0;
	constant SUPPRESS  : integer := 1;
	constant CHIPSEL   : integer := 2;
begin
	-- Infer registers
	process(clk_in)
	begin
		if ( rising_edge(clk_in) ) then
			config <= config_next;
		end if;
	end process;
	
	config_next <=
		h2fData_in(NUM_DEVS+1 downto 0) when h2fValid_in = '1' and chanAddr_in /= "0000000"
		else config;

	sendData <= h2fData_in;
	sendValid <= h2fValid_in when chanAddr_in = "0000000" else '0';
	h2fReady_out <= sendReady when chanAddr_in = "0000000" else '1';

	f2hData_out <=
		fifoData when chanAddr_in = "0000000"
		else std_logic_vector(resize(unsigned(config), 8));
	f2hValid_out <=
		fifoValid when chanAddr_in = "0000000"
		else '1';
	fifoReady <=
		f2hReady_in when chanAddr_in = "0000000"
		else '0';

	spiCS_out <= not config(CHIPSEL+NUM_DEVS-1 downto CHIPSEL);
	
	spi_master : entity work.spi_master
		generic map(
			SLOW_COUNT => "111011",  -- spiClk = sysClk/120 (400kHz @48MHz)
			FAST_COUNT => "000000",  -- spiClk = sysClk/2 (24MHz @48MHz)
			BIT_ORDER  => '1'        -- MSB first
		)
		port map(
			reset_in       => '0',
			clk_in         => clk_in,

			-- Send pipe
			turbo_in       => config(TURBO),
			suppress_in    => config(SUPPRESS),
			sendData_in    => sendData,
			sendValid_in   => sendValid,
			sendReady_out  => sendReady,

			-- Receive pipe
			recvData_out   => recvData,
			recvValid_out  => recvValid,
			recvReady_in   => recvReady,

			-- SPI interface
			spiClk_out     => spiClk_out,
			spiData_out    => spiData_out,
			spiData_in     => spiData_in
		);

	recv_fifo : entity work.fifo_wrapper
		port map(
			clk_in          => clk_in,

			-- Production end
			inputData_in    => recvData,
			inputValid_in   => recvValid,
			inputReady_out  => recvReady,

			-- Consumption end
			outputData_out  => fifoData,
			outputValid_out => fifoValid,
			outputReady_in  => fifoReady
		);
	
end architecture;
